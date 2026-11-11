// Bluetooth LE HID keyboard, used to type the displayed OTP code into a host.
//
// A minimal boot-protocol keyboard built directly on NimBLE's HID helper: the
// device only ever sends digits, so there is no need for a full keyboard
// library or a keymap.

#ifndef BLE_HID_H
#define BLE_HID_H

#include <NimBLEDevice.h>
#include <NimBLEHIDDevice.h>
#include <NimBLEServer.h>

#define BLE_HID_DEVICE_NAME "OXOTP+"
#define BLE_HID_REPORT_ID   1

// Delay between key down and key up, and between two keys. Slow enough for
// hosts to register every keystroke, fast enough to stay well inside the 30s
// window of the code being typed.
#define BLE_HID_KEY_DELAY_MS 12

// USB HID keyboard usage IDs for the digit row.
static const uint8_t ble_hid_digit_keys[10] = {
  0x27,  // '0'
  0x1E,  // '1'
  0x1F,  // '2'
  0x20,  // '3'
  0x21,  // '4'
  0x22,  // '5'
  0x23,  // '6'
  0x24,  // '7'
  0x25,  // '8'
  0x26   // '9'
};

// Boot protocol keyboard report descriptor: 1 byte modifiers, 1 reserved byte,
// 5 LED bits (+3 padding) as output, and 6 concurrent key slots as input.
static const uint8_t ble_hid_report_map[] = {
  0x05, 0x01,                    // Usage Page (Generic Desktop)
  0x09, 0x06,                    // Usage (Keyboard)
  0xA1, 0x01,                    // Collection (Application)
  0x85, BLE_HID_REPORT_ID,       //   Report ID (1)
  0x05, 0x07,                    //   Usage Page (Keyboard)
  0x19, 0xE0,                    //   Usage Minimum (Left Control)
  0x29, 0xE7,                    //   Usage Maximum (Right GUI)
  0x15, 0x00,                    //   Logical Minimum (0)
  0x25, 0x01,                    //   Logical Maximum (1)
  0x75, 0x01,                    //   Report Size (1)
  0x95, 0x08,                    //   Report Count (8)
  0x81, 0x02,                    //   Input (Data, Variable, Absolute) - modifiers
  0x95, 0x01,                    //   Report Count (1)
  0x75, 0x08,                    //   Report Size (8)
  0x81, 0x03,                    //   Input (Constant) - reserved byte
  0x95, 0x05,                    //   Report Count (5)
  0x75, 0x01,                    //   Report Size (1)
  0x05, 0x08,                    //   Usage Page (LEDs)
  0x19, 0x01,                    //   Usage Minimum (Num Lock)
  0x29, 0x05,                    //   Usage Maximum (Kana)
  0x91, 0x02,                    //   Output (Data, Variable, Absolute) - LEDs
  0x95, 0x01,                    //   Report Count (1)
  0x75, 0x03,                    //   Report Size (3)
  0x91, 0x03,                    //   Output (Constant) - LED padding
  0x95, 0x06,                    //   Report Count (6)
  0x75, 0x08,                    //   Report Size (8)
  0x15, 0x00,                    //   Logical Minimum (0)
  0x25, 0x65,                    //   Logical Maximum (101)
  0x05, 0x07,                    //   Usage Page (Keyboard)
  0x19, 0x00,                    //   Usage Minimum (0)
  0x29, 0x65,                    //   Usage Maximum (101)
  0x81, 0x00,                    //   Input (Data, Array) - pressed keys
  0xC0                           // End Collection
};

NimBLEHIDDevice* ble_hid_device = nullptr;
NimBLECharacteristic* ble_hid_input = nullptr;
bool ble_hid_connected = false;

class BleHidServerCallbacks : public NimBLEServerCallbacks {
  void onConnect(NimBLEServer* server) {
    ble_hid_connected = true;
  }

  void onDisconnect(NimBLEServer* server) {
    ble_hid_connected = false;
    // Make the device discoverable again so the host can reconnect.
    NimBLEDevice::startAdvertising();
  }
};

bool bleHidConnected() {
  return ble_hid_connected;
}

// Bring up the BLE HID keyboard and start advertising.
void bleHidBegin() {
  NimBLEDevice::init(BLE_HID_DEVICE_NAME);

  // Bond with the host so it can reconnect without re-pairing. No MITM
  // protection: the device has no keypad to enter a passkey on.
  NimBLEDevice::setSecurityAuth(true, false, true);

  NimBLEServer* server = NimBLEDevice::createServer();
  server->setCallbacks(new BleHidServerCallbacks());

  ble_hid_device = new NimBLEHIDDevice(server);
  ble_hid_input = ble_hid_device->inputReport(BLE_HID_REPORT_ID);

  ble_hid_device->manufacturer()->setValue("OXOTP+");
  ble_hid_device->pnp(0x02, 0xE502, 0xA111, 0x0210);
  ble_hid_device->hidInfo(0x00, 0x01);
  ble_hid_device->reportMap((uint8_t*)ble_hid_report_map, sizeof(ble_hid_report_map));
  ble_hid_device->startServices();

  NimBLEAdvertising* advertising = NimBLEDevice::getAdvertising();
  advertising->setAppearance(HID_KEYBOARD);
  advertising->addServiceUUID(ble_hid_device->hidService()->getUUID());
  advertising->start();
}

// Send one key down + key up pair.
void bleHidSendKey(uint8_t keycode) {
  uint8_t report[8] = {0};

  report[2] = keycode;
  ble_hid_input->setValue(report, sizeof(report));
  ble_hid_input->notify();
  delay(BLE_HID_KEY_DELAY_MS);

  report[2] = 0;
  ble_hid_input->setValue(report, sizeof(report));
  ble_hid_input->notify();
  delay(BLE_HID_KEY_DELAY_MS);
}

// Type an OTP code. Returns false when no host is connected. Non-digits are
// skipped, so this stays safe if code formatting ever changes.
bool bleHidTypeCode(const String& code) {
  if (!ble_hid_connected || ble_hid_input == nullptr) {
    return false;
  }

  for (unsigned int i = 0; i < code.length(); i++) {
    char c = code.charAt(i);
    if (c >= '0' && c <= '9') {
      bleHidSendKey(ble_hid_digit_keys[c - '0']);
    }
  }

  return true;
}

#endif
