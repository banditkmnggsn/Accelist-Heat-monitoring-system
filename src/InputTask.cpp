#include "InputTask.h"

#include "app_config.h"
#include "pins.h"

namespace {

constexpr UBaseType_t kQueueLength = 16;

bool doorClosedLevel(int level) { return level == PIN_DOOR_ACTIVE_LEVEL; }

}  // namespace

InputTask::InputTask()
    : decoder_(app::input::kEncStepsPerDetent, app::input::kEncReverse),
      button_(app::input::kButtonDebounceMs),
      door_(app::input::kDoorDebounceMs),
      gesture_(app::input::kPressMaxMs, app::input::kHoldBarMs) {}

void InputTask::begin(bool soundOn) {
    buzzer_.begin(soundOn);

    // Pull-up eksternal 10k sudah ada di board; pull-up internal hanya
    // cadangan. GPIO34 tidak punya pull-up internal (R8 eksternal).
    pinMode(PIN_ENC_A, INPUT_PULLUP);
    pinMode(PIN_ENC_B, INPUT_PULLUP);
    pinMode(PIN_ENC_SW, INPUT_PULLUP);
    pinMode(PIN_DOOR, INPUT);
    pinMode(PIN_START_BTN, INPUT);  // dipesan, belum dibaca

    const uint32_t now = millis();
    decoder_.reset(readAB());
    const bool pressed = digitalRead(PIN_ENC_SW) == LOW;
    button_.reset(pressed, now);
    gesture_.begin(pressed, now);
    const int doorLevel = digitalRead(PIN_DOOR);
    door_.reset(doorClosedLevel(doorLevel), now);
    doorClosed_.store(door_.state());

    queue_ = xQueueCreate(kQueueLength, sizeof(hbinput::InputEvent));
    xTaskCreatePinnedToCore(entry, "input", app::task::kInputStackBytes, this, app::task::kInputPriority,
                            nullptr, app::task::kInputCore);
}

void InputTask::entry(void* self) { static_cast<InputTask*>(self)->run(); }

void InputTask::run() {
    TickType_t wake = xTaskGetTickCount();
    for (;;) {
        poll(millis());
        vTaskDelayUntil(&wake, pdMS_TO_TICKS(app::input::kPollPeriodMs));
    }
}

uint8_t InputTask::readAB() const {
    return static_cast<uint8_t>(((digitalRead(PIN_ENC_A) == HIGH) ? 2 : 0) |
                                ((digitalRead(PIN_ENC_B) == HIGH) ? 1 : 0));
}

void InputTask::poll(uint32_t nowMs) {
    buzzer_.update(nowMs);

    const int8_t step = decoder_.update(readAB());
    const bool pressed = button_.update(digitalRead(PIN_ENC_SW) == LOW, nowMs);
    doorClosed_.store(door_.update(doorClosedLevel(digitalRead(PIN_DOOR)), nowMs));

    gesture_.setHoldTarget(holdTarget_.load());
    const bool wasDown = gesture_.isDown();
    const hbinput::Gesture g = gesture_.update(nowMs, pressed, step != 0);
    holdProgress_.store(gesture_.holdProgressMs(nowMs));
    buttonDown_.store(gesture_.isDown());

    // Putar sambil tombol ditekan diabaikan (dan membatalkan tekanan itu)
    const bool rotateAccepted = step != 0 && !wasDown && !gesture_.isDown();
    hbinput::InputEvent event{};
    event.ms = nowMs;
    if (rotateAccepted) {
        event.type = hbinput::InputType::Rotate;
        event.steps = step;
        xQueueSend(queue_, &event, 0);
        detents_.fetch_add(step);
    } else if (g == hbinput::Gesture::Press) {
        event.type = hbinput::InputType::Press;
        xQueueSend(queue_, &event, 0);
        buttonLatch_.store('S');
    } else if (g == hbinput::Gesture::Hold) {
        event.type = hbinput::InputType::Hold;
        xQueueSend(queue_, &event, 0);
        buttonLatch_.store('L');
    }
}

bool InputTask::nextEvent(hbinput::InputEvent& out) {
    return queue_ != nullptr && xQueueReceive(queue_, &out, 0) == pdTRUE;
}

char InputTask::takeButtonChar() {
    const char latched = buttonLatch_.exchange('-');
    if (latched != '-') {
        return latched;
    }
    return buttonDown_.load() ? 'P' : '-';
}
