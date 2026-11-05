// =====================================================================
//  test_input — lib/HeatboxInput: decoder rotary tanpa langkah palsu,
//  debounce, dan gesture TEKAN / TAHAN / batal.
//  Jalan di PC (env native / g++ / MSVC) dan di ESP32.
// =====================================================================

#ifdef ARDUINO
#include <Arduino.h>
#endif

#include <ButtonGesture.h>
#include <Debouncer.h>
#include <QuadratureDecoder.h>
#include <unity.h>

using hbinput::ButtonGesture;
using hbinput::Debouncer;
using hbinput::Gesture;
using hbinput::QuadratureDecoder;

namespace {

// Satu klik penuh searah (+1) untuk mode 4 langkah, mulai & berakhir di 11:
// 11 -> 01 -> 00 -> 10 -> 11
const uint8_t kCw[] = {0x1, 0x0, 0x2, 0x3};
// Arah sebaliknya: 11 -> 10 -> 00 -> 01 -> 11
const uint8_t kCcw[] = {0x2, 0x0, 0x1, 0x3};

int feed(QuadratureDecoder& d, const uint8_t* seq, int n) {
    int sum = 0;
    for (int i = 0; i < n; ++i) {
        sum += d.update(seq[i]);
    }
    return sum;
}

}  // namespace

void setUp() {}
void tearDown() {}

void test_full_click_cw_gives_plus_one() {
    QuadratureDecoder d(4, false);
    d.reset(0x3);
    TEST_ASSERT_EQUAL_INT(0, d.update(0x1));
    TEST_ASSERT_EQUAL_INT(0, d.update(0x0));
    TEST_ASSERT_EQUAL_INT(0, d.update(0x2));
    TEST_ASSERT_EQUAL_INT(1, d.update(0x3));
    TEST_ASSERT_EQUAL_INT(4, d.rawCount());
}

void test_full_click_ccw_gives_minus_one() {
    QuadratureDecoder d(4, false);
    d.reset(0x3);
    TEST_ASSERT_EQUAL_INT(-1, feed(d, kCcw, 4));
}

void test_reverse_flag_flips_direction() {
    QuadratureDecoder d(4, true);
    d.reset(0x3);
    TEST_ASSERT_EQUAL_INT(-1, feed(d, kCw, 4));
}

void test_bounce_at_detent_gives_no_step() {
    QuadratureDecoder d(4, false);
    d.reset(0x3);
    // Pantulan kontak: 11 -> 01 -> 11 -> 01 -> 11 berulang
    for (int i = 0; i < 20; ++i) {
        TEST_ASSERT_EQUAL_INT(0, d.update(0x1));
        TEST_ASSERT_EQUAL_INT(0, d.update(0x3));
    }
    TEST_ASSERT_EQUAL_INT(0, d.rawCount());
}

void test_bounce_inside_click_still_one_step() {
    QuadratureDecoder d(4, false);
    d.reset(0x3);
    // 11 -> 01 -> 00 -> 01 -> 00 (pantulan) -> 10 -> 11
    const uint8_t seq[] = {0x1, 0x0, 0x1, 0x0, 0x2, 0x3};
    TEST_ASSERT_EQUAL_INT(1, feed(d, seq, 6));
}

void test_half_turn_back_gives_nothing() {
    QuadratureDecoder d(4, false);
    d.reset(0x3);
    // setengah jalan lalu kembali: tidak boleh jadi langkah ke mana pun
    const uint8_t seq[] = {0x1, 0x0, 0x1, 0x3};
    TEST_ASSERT_EQUAL_INT(0, feed(d, seq, 4));
    TEST_ASSERT_EQUAL_UINT32(0, d.partialReturns());  // hitungan kembali ke 0
}

void test_missed_transition_never_reverses() {
    QuadratureDecoder d(4, false);
    d.reset(0x3);
    // putar kanan cepat, 00 terlewat: 11 -> 01 -> 10 (lompatan) -> 11
    const uint8_t seq[] = {0x1, 0x2, 0x3};
    const int sum = feed(d, seq, 3);
    TEST_ASSERT_TRUE(sum >= 0);  // boleh hilang, tidak boleh -1
    TEST_ASSERT_EQUAL_UINT32(1, d.invalidJumps());
    TEST_ASSERT_EQUAL_UINT32(1, d.partialReturns());
}

void test_many_cw_clicks_only_increase() {
    QuadratureDecoder d(4, false);
    d.reset(0x3);
    int pos = 0;
    for (int click = 0; click < 50; ++click) {
        for (int i = 0; i < 4; ++i) {
            const int8_t step = d.update(kCw[i]);
            TEST_ASSERT_TRUE(step >= 0);
            pos += step;
        }
    }
    TEST_ASSERT_EQUAL_INT(50, pos);
}

void test_two_steps_per_detent_mode() {
    QuadratureDecoder d(2, false);
    d.reset(0x3);
    // 11 -> 01 -> 00 = satu klik, 00 -> 10 -> 11 = klik berikutnya
    TEST_ASSERT_EQUAL_INT(0, d.update(0x1));
    TEST_ASSERT_EQUAL_INT(1, d.update(0x0));
    TEST_ASSERT_EQUAL_INT(0, d.update(0x2));
    TEST_ASSERT_EQUAL_INT(1, d.update(0x3));
    TEST_ASSERT_EQUAL_INT(-1, feed(d, kCcw, 2));  // 11 -> 10 -> 00
}

void test_debouncer_ignores_short_glitch() {
    Debouncer db(20);
    db.reset(false, 0);
    TEST_ASSERT_FALSE(db.update(true, 5));
    TEST_ASSERT_FALSE(db.update(false, 10));  // glitch 5 ms
    TEST_ASSERT_FALSE(db.update(false, 40));
    TEST_ASSERT_FALSE(db.update(true, 50));
    TEST_ASSERT_FALSE(db.update(true, 69));
    TEST_ASSERT_TRUE(db.update(true, 70));
}

void test_debouncer_handles_millis_wrap() {
    Debouncer db(20);
    const uint32_t t0 = 0xFFFFFFF0u;
    db.reset(false, t0);
    db.update(true, t0 + 5);
    TEST_ASSERT_TRUE(db.update(true, t0 + 30));  // melewati 0
}

void test_short_press_on_release() {
    ButtonGesture g(600, 600);
    g.begin(false, 0);
    g.setHoldTarget(2000);
    TEST_ASSERT_EQUAL(Gesture::None, g.update(100, true, false));
    TEST_ASSERT_EQUAL(Gesture::None, g.update(400, true, false));
    TEST_ASSERT_EQUAL(Gesture::Press, g.update(500, false, false));
}

void test_release_between_press_and_hold_is_nothing() {
    ButtonGesture g(600, 600);
    g.begin(false, 0);
    g.setHoldTarget(2000);
    g.update(0, true, false);
    TEST_ASSERT_EQUAL(Gesture::None, g.update(1500, false, false));
}

void test_hold_fires_once_at_target_then_needs_release() {
    ButtonGesture g(600, 600);
    g.begin(false, 0);
    g.setHoldTarget(2000);
    g.update(0, true, false);
    TEST_ASSERT_EQUAL_UINT32(0, g.holdProgressMs(500));     // bar belum tampil
    TEST_ASSERT_EQUAL_UINT32(700, g.holdProgressMs(700));   // bar tampil
    TEST_ASSERT_EQUAL(Gesture::None, g.update(1999, true, false));
    TEST_ASSERT_EQUAL(Gesture::Hold, g.update(2000, true, false));
    // masih ditahan: target baru (mis. 5 dtk di layar baru) tidak memicu lagi
    g.setHoldTarget(5000);
    TEST_ASSERT_EQUAL(Gesture::None, g.update(6000, true, false));
    TEST_ASSERT_EQUAL_UINT32(0, g.holdProgressMs(6000));
    TEST_ASSERT_EQUAL(Gesture::None, g.update(6100, false, false));  // lepas: tidak ada TEKAN
    TEST_ASSERT_FALSE(g.isDown());
}

void test_service_hold_ignores_two_seconds() {
    ButtonGesture g(600, 600);
    g.begin(false, 0);
    g.setHoldTarget(5000);
    g.update(0, true, false);
    TEST_ASSERT_EQUAL(Gesture::None, g.update(2000, true, false));
    TEST_ASSERT_EQUAL(Gesture::Hold, g.update(5000, true, false));
}

void test_rotate_while_pressed_cancels_everything() {
    ButtonGesture g(600, 600);
    g.begin(false, 0);
    g.setHoldTarget(2000);
    g.update(0, true, false);
    g.update(100, true, true);  // putar sambil tekan
    TEST_ASSERT_TRUE(g.wasCancelled());
    TEST_ASSERT_EQUAL_UINT32(0, g.holdProgressMs(1000));
    TEST_ASSERT_EQUAL(Gesture::None, g.update(3000, true, false));  // tidak ada TAHAN
    TEST_ASSERT_EQUAL(Gesture::None, g.update(3100, false, false)); // tidak ada TEKAN
}

void test_no_hold_target_means_no_bar_and_no_hold() {
    ButtonGesture g(600, 600);
    g.begin(false, 0);
    g.setHoldTarget(0);
    g.update(0, true, false);
    TEST_ASSERT_EQUAL_UINT32(0, g.holdProgressMs(3000));
    TEST_ASSERT_EQUAL(Gesture::None, g.update(9000, true, false));
}

void test_pressed_at_boot_gives_no_event() {
    ButtonGesture g(600, 600);
    g.begin(true, 0);
    g.setHoldTarget(2000);
    TEST_ASSERT_EQUAL(Gesture::None, g.update(100, true, false));
    TEST_ASSERT_EQUAL(Gesture::None, g.update(3000, true, false));
    TEST_ASSERT_EQUAL(Gesture::None, g.update(3100, false, false));
    g.update(4000, true, false);
    TEST_ASSERT_EQUAL(Gesture::Press, g.update(4100, false, false));
}

int runAll() {
    UNITY_BEGIN();
    RUN_TEST(test_full_click_cw_gives_plus_one);
    RUN_TEST(test_full_click_ccw_gives_minus_one);
    RUN_TEST(test_reverse_flag_flips_direction);
    RUN_TEST(test_bounce_at_detent_gives_no_step);
    RUN_TEST(test_bounce_inside_click_still_one_step);
    RUN_TEST(test_half_turn_back_gives_nothing);
    RUN_TEST(test_missed_transition_never_reverses);
    RUN_TEST(test_many_cw_clicks_only_increase);
    RUN_TEST(test_two_steps_per_detent_mode);
    RUN_TEST(test_debouncer_ignores_short_glitch);
    RUN_TEST(test_debouncer_handles_millis_wrap);
    RUN_TEST(test_short_press_on_release);
    RUN_TEST(test_release_between_press_and_hold_is_nothing);
    RUN_TEST(test_hold_fires_once_at_target_then_needs_release);
    RUN_TEST(test_service_hold_ignores_two_seconds);
    RUN_TEST(test_rotate_while_pressed_cancels_everything);
    RUN_TEST(test_no_hold_target_means_no_bar_and_no_hold);
    RUN_TEST(test_pressed_at_boot_gives_no_event);
    return UNITY_END();
}

#ifdef ARDUINO
void setup() {
    delay(2000);
    runAll();
}
void loop() {}
#else
int main() { return runAll(); }
#endif
