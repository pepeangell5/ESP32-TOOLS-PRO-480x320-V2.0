#include "IrVirtualRemotes.h"

#include <Arduino.h>
#include <Preferences.h>

#include "MenuSystem.h"
#include "PepeDraw.h"
#include "Pins.h"
#include "SignalTools.h"
#include "VirtualKeyboard.h"

static constexpr uint8_t REMOTE_MAX = 4;
static constexpr uint8_t REMOTE_BUTTON_MAX = 8;
static constexpr uint8_t REMOTE_NAME_MAX = 14;
static constexpr uint8_t BUTTON_NAME_MAX = 14;
static constexpr const char* REMOTE_NAMESPACE = "irremotes";

struct RemoteButton {
    String name;
    uint8_t savedSlot = 255;
};

static String remoteNameKey(uint8_t remote) {
    return String("r") + String(remote);
}

static String buttonNameKey(uint8_t remote, uint8_t button) {
    return String("n") + String(remote) + String(button);
}

static String buttonSlotKey(uint8_t remote, uint8_t button) {
    return String("s") + String(remote) + String(button);
}

static void drawRemoteFrame(const char* title) {
    tft.fillScreen(TFT_BLACK);
    tft.drawRect(0, 0, 320, 240, TFT_WHITE);
    tft.drawFastHLine(0, 34, 320, TFT_WHITE);
    tft.drawFastHLine(0, 212, 320, TFT_WHITE);
    drawStringBig(10, 8, title, TFT_WHITE, 1);
    drawStringCustom(10, 220, "OK: RETURN", TFT_WHITE, 1);
}

static void showRemoteMessage(const char* title, const String& line1,
                              const String& line2, uint16_t color) {
    drawRemoteFrame(title);
    drawStringCustom(24, 82, line1, color, 2);
    if (line2.length() > 0) {
        drawStringFit(22, 116, line2, TFT_WHITE, 276, 1);
    }
    while (digitalRead(BTN_OK) == HIGH) delay(20);
    while (digitalRead(BTN_OK) == LOW) delay(5);
    delay(60);
}

static String trimName(String name, uint8_t maxLen) {
    name.trim();
    if (name.length() > maxLen) name = name.substring(0, maxLen);
    return name;
}

static bool loadRemoteName(uint8_t remote, String* name) {
    if (remote >= REMOTE_MAX) return false;

    Preferences prefs;
    if (!prefs.begin(REMOTE_NAMESPACE, true)) return false;
    String value = prefs.getString(remoteNameKey(remote).c_str(), "");
    prefs.end();

    value.trim();
    if (value.length() == 0) return false;
    if (name) *name = value;
    return true;
}

static bool saveRemoteName(uint8_t remote, const String& requestedName) {
    if (remote >= REMOTE_MAX) return false;
    String name = trimName(requestedName, REMOTE_NAME_MAX);
    if (name.length() == 0) return false;

    Preferences prefs;
    if (!prefs.begin(REMOTE_NAMESPACE, false)) return false;
    bool ok = prefs.putString(remoteNameKey(remote).c_str(), name) > 0;
    prefs.end();
    return ok;
}

static int findEmptyRemote() {
    for (uint8_t i = 0; i < REMOTE_MAX; i++) {
        if (!loadRemoteName(i, nullptr)) return i;
    }
    return -1;
}

static bool loadButton(uint8_t remote, uint8_t button, RemoteButton* out) {
    if (remote >= REMOTE_MAX || button >= REMOTE_BUTTON_MAX) return false;

    Preferences prefs;
    if (!prefs.begin(REMOTE_NAMESPACE, true)) return false;
    String name = prefs.getString(buttonNameKey(remote, button).c_str(), "");
    uint32_t slot = prefs.getUInt(buttonSlotKey(remote, button).c_str(), 255);
    prefs.end();

    name.trim();
    if (name.length() == 0 || slot >= signalToolsSavedIrMax()) return false;
    if (out) {
        out->name = name;
        out->savedSlot = static_cast<uint8_t>(slot);
    }
    return true;
}

static bool saveButton(uint8_t remote, uint8_t button,
                       const String& requestedName, uint8_t savedSlot) {
    if (remote >= REMOTE_MAX || button >= REMOTE_BUTTON_MAX) return false;
    if (savedSlot >= signalToolsSavedIrMax()) return false;

    String name = trimName(requestedName, BUTTON_NAME_MAX);
    if (name.length() == 0) return false;

    Preferences prefs;
    if (!prefs.begin(REMOTE_NAMESPACE, false)) return false;
    bool ok = prefs.putString(buttonNameKey(remote, button).c_str(), name) > 0;
    ok = (prefs.putUInt(buttonSlotKey(remote, button).c_str(), savedSlot) > 0) && ok;
    prefs.end();
    return ok;
}

static bool deleteButton(uint8_t remote, uint8_t button) {
    if (remote >= REMOTE_MAX || button >= REMOTE_BUTTON_MAX) return false;

    Preferences prefs;
    if (!prefs.begin(REMOTE_NAMESPACE, false)) return false;
    prefs.remove(buttonNameKey(remote, button).c_str());
    prefs.remove(buttonSlotKey(remote, button).c_str());
    prefs.end();
    return true;
}

static int findEmptyButton(uint8_t remote) {
    for (uint8_t i = 0; i < REMOTE_BUTTON_MAX; i++) {
        if (!loadButton(remote, i, nullptr)) return i;
    }
    return -1;
}

static bool deleteRemote(uint8_t remote) {
    if (remote >= REMOTE_MAX) return false;

    Preferences prefs;
    if (!prefs.begin(REMOTE_NAMESPACE, false)) return false;
    prefs.remove(remoteNameKey(remote).c_str());
    for (uint8_t i = 0; i < REMOTE_BUTTON_MAX; i++) {
        prefs.remove(buttonNameKey(remote, i).c_str());
        prefs.remove(buttonSlotKey(remote, i).c_str());
    }
    prefs.end();
    return true;
}

static bool confirmDelete(const String& label) {
    static const char* actions[] = {
        "Cancel",
        "Delete"
    };

    drawRemoteFrame("DELETE");
    drawStringCustom(22, 58, "Delete?", TFT_YELLOW, 2);
    drawStringFit(22, 92, label, TFT_WHITE, 276, 1);
    delay(250);
    int choice = runSubMenu("DELETE", actions, 2, false);
    return choice == 1;
}

static int selectSavedSignal() {
    String labels[8];
    const char* items[8];
    uint8_t slots[8];
    int itemCount = 0;

    uint8_t maxSlots = signalToolsSavedIrMax();
    for (uint8_t slot = 0; slot < maxSlots && itemCount < 8; slot++) {
        String name;
        uint16_t count = 0;
        if (signalToolsLoadSavedIrInfo(slot, &name, &count)) {
            labels[itemCount] = String(slot + 1) + ": " + name;
            items[itemCount] = labels[itemCount].c_str();
            slots[itemCount] = slot;
            itemCount++;
        }
    }

    if (itemCount == 0) {
        showRemoteMessage("IR REMOTES", "NO SAVED IR",
                          "Save captures first.", TFT_YELLOW);
        return -1;
    }

    int choice = runSubMenu("PICK SIGNAL", items, itemCount);
    if (choice < 0) return -1;
    return slots[choice];
}

static void createRemote() {
    int remote = findEmptyRemote();
    if (remote < 0) {
        showRemoteMessage("IR REMOTES", "REMOTE FULL",
                          "Delete a remote first.", TFT_RED);
        return;
    }

    String name = virtualKeyboardInput("REMOTE NAME", "Example: FAN",
                                       REMOTE_NAME_MAX, false);
    name = trimName(name, REMOTE_NAME_MAX);
    if (name.length() == 0) {
        showRemoteMessage("IR REMOTES", "CANCELED", "", TFT_YELLOW);
        return;
    }

    if (saveRemoteName(remote, name)) {
        showRemoteMessage("IR REMOTES", "CREATED", name, TFT_GREEN);
    } else {
        showRemoteMessage("IR REMOTES", "CREATE FAILED", "", TFT_RED);
    }
}

static void addButtonToRemote(uint8_t remote) {
    int button = findEmptyButton(remote);
    if (button < 0) {
        showRemoteMessage("IR REMOTES", "BUTTON FULL",
                          "Delete a button first.", TFT_RED);
        return;
    }

    int savedSlot = selectSavedSignal();
    if (savedSlot < 0) return;

    String savedName;
    uint16_t count = 0;
    signalToolsLoadSavedIrInfo(savedSlot, &savedName, &count);
    String name = trimName(savedName, BUTTON_NAME_MAX);
    if (name.length() == 0) name = "Button";

    if (saveButton(remote, button, name, static_cast<uint8_t>(savedSlot))) {
        showRemoteMessage("IR REMOTES", "BUTTON ADDED", name, TFT_GREEN);
    } else {
        showRemoteMessage("IR REMOTES", "ADD FAILED", "", TFT_RED);
    }
}

static void replayRemoteButton(const RemoteButton& button) {
    drawRemoteFrame("IR REMOTE");
    drawStringCustom(22, 72, "Sending", TFT_CYAN, 1);
    drawStringFit(22, 94, button.name, TFT_WHITE, 276, 2);

    bool ok = signalToolsReplaySavedIrSlot(button.savedSlot);
    drawStringCustom(22, 138, ok ? "SENT" : "SEND FAILED",
                     ok ? TFT_GREEN : TFT_RED, 2);
    while (digitalRead(BTN_OK) == HIGH) delay(20);
    while (digitalRead(BTN_OK) == LOW) delay(5);
    delay(60);
}

static void renameRemote(uint8_t remote, const String& currentName) {
    String name = virtualKeyboardInput("RENAME REMOTE", currentName,
                                       REMOTE_NAME_MAX, false);
    name = trimName(name, REMOTE_NAME_MAX);
    if (name.length() == 0) {
        showRemoteMessage("IR REMOTES", "CANCELED", "", TFT_YELLOW);
        return;
    }

    if (saveRemoteName(remote, name)) {
        showRemoteMessage("IR REMOTES", "RENAMED", name, TFT_GREEN);
    } else {
        showRemoteMessage("IR REMOTES", "RENAME FAILED", "", TFT_RED);
    }
}

static void renameButton(uint8_t remote, uint8_t button,
                         const RemoteButton& currentButton) {
    String name = virtualKeyboardInput("BUTTON NAME", currentButton.name,
                                       BUTTON_NAME_MAX, false);
    name = trimName(name, BUTTON_NAME_MAX);
    if (name.length() == 0) {
        showRemoteMessage("IR REMOTES", "CANCELED", "", TFT_YELLOW);
        return;
    }

    if (saveButton(remote, button, name, currentButton.savedSlot)) {
        showRemoteMessage("IR REMOTES", "RENAMED", name, TFT_GREEN);
    } else {
        showRemoteMessage("IR REMOTES", "RENAME FAILED", "", TFT_RED);
    }
}

static void reassignButton(uint8_t remote, uint8_t button,
                           const RemoteButton& currentButton) {
    int savedSlot = selectSavedSignal();
    if (savedSlot < 0) return;

    if (saveButton(remote, button, currentButton.name,
                   static_cast<uint8_t>(savedSlot))) {
        showRemoteMessage("IR REMOTES", "REASSIGNED",
                          currentButton.name, TFT_GREEN);
    } else {
        showRemoteMessage("IR REMOTES", "REASSIGN FAILED", "", TFT_RED);
    }
}

static void manageOneButton(uint8_t remote, uint8_t button) {
    static const char* actions[] = {
        "Replay",
        "Rename",
        "Reassign",
        "Delete",
        "Back"
    };

    while (true) {
        RemoteButton currentButton;
        if (!loadButton(remote, button, &currentButton)) {
            showRemoteMessage("IR REMOTES", "NOT FOUND", "", TFT_RED);
            return;
        }

        int choice = runSubMenu("BUTTON EDIT", actions, 5);
        switch (choice) {
            case -1:
            case  4:
                return;
            case  0:
                replayRemoteButton(currentButton);
                break;
            case  1:
                renameButton(remote, button, currentButton);
                break;
            case  2:
                reassignButton(remote, button, currentButton);
                break;
            case  3:
                if (confirmDelete(currentButton.name)) {
                    deleteButton(remote, button);
                    showRemoteMessage("IR REMOTES", "DELETED",
                                      currentButton.name, TFT_GREEN);
                    return;
                }
                break;
        }
    }
}

static void manageButtons(uint8_t remote) {
    while (true) {
        String labels[REMOTE_BUTTON_MAX];
        const char* items[REMOTE_BUTTON_MAX];
        uint8_t buttons[REMOTE_BUTTON_MAX];
        int itemCount = 0;

        for (uint8_t i = 0; i < REMOTE_BUTTON_MAX; i++) {
            RemoteButton button;
            if (loadButton(remote, i, &button)) {
                labels[itemCount] = button.name;
                items[itemCount] = labels[itemCount].c_str();
                buttons[itemCount] = i;
                itemCount++;
            }
        }

        if (itemCount == 0) {
            showRemoteMessage("IR REMOTES", "NO BUTTONS",
                              "Add a button first.", TFT_YELLOW);
            return;
        }

        int choice = runSubMenu("MANAGE BTN", items, itemCount);
        if (choice < 0) return;
        manageOneButton(remote, buttons[choice]);
    }
}

static void runRemote(uint8_t remote) {
    while (true) {
        String remoteName;
        if (!loadRemoteName(remote, &remoteName)) return;

        String labels[REMOTE_BUTTON_MAX + 4];
        const char* items[REMOTE_BUTTON_MAX + 4];
        RemoteButton buttons[REMOTE_BUTTON_MAX];
        int buttonCount = 0;
        int itemCount = 0;

        for (uint8_t i = 0; i < REMOTE_BUTTON_MAX; i++) {
            RemoteButton button;
            if (loadButton(remote, i, &button)) {
                buttons[buttonCount] = button;
                labels[itemCount] = button.name;
                items[itemCount] = labels[itemCount].c_str();
                buttonCount++;
                itemCount++;
            }
        }

        int addIndex = itemCount;
        labels[itemCount] = "Add Button";
        items[itemCount++] = labels[addIndex].c_str();

        int manageIndex = itemCount;
        labels[itemCount] = "Manage Buttons";
        items[itemCount++] = labels[manageIndex].c_str();

        int renameIndex = itemCount;
        labels[itemCount] = "Rename Remote";
        items[itemCount++] = labels[renameIndex].c_str();

        int deleteIndex = itemCount;
        labels[itemCount] = "Delete Remote";
        items[itemCount++] = labels[deleteIndex].c_str();

        int choice = runSubMenu(remoteName.c_str(), items, itemCount);
        if (choice < 0) return;

        if (choice < buttonCount) {
            replayRemoteButton(buttons[choice]);
        } else if (choice == addIndex) {
            addButtonToRemote(remote);
        } else if (choice == manageIndex) {
            manageButtons(remote);
        } else if (choice == renameIndex) {
            renameRemote(remote, remoteName);
        } else if (choice == deleteIndex) {
            if (confirmDelete(remoteName)) {
                deleteRemote(remote);
                showRemoteMessage("IR REMOTES", "DELETED",
                                  remoteName, TFT_GREEN);
                return;
            }
        }
    }
}

void runIrVirtualRemotes() {
    while (true) {
        String labels[REMOTE_MAX + 1];
        const char* items[REMOTE_MAX + 1];
        uint8_t remotes[REMOTE_MAX];
        int itemCount = 0;

        for (uint8_t i = 0; i < REMOTE_MAX; i++) {
            String name;
            if (loadRemoteName(i, &name)) {
                labels[itemCount] = name;
                items[itemCount] = labels[itemCount].c_str();
                remotes[itemCount] = i;
                itemCount++;
            }
        }

        int addIndex = -1;
        if (findEmptyRemote() >= 0) {
            addIndex = itemCount;
            labels[itemCount] = "Add Remote";
            items[itemCount++] = labels[addIndex].c_str();
        }

        if (itemCount == 0) {
            showRemoteMessage("IR REMOTES", "NO REMOTES",
                              "Create one first.", TFT_YELLOW);
            return;
        }

        int choice = runSubMenu("IR REMOTES", items, itemCount);
        if (choice < 0) return;

        if (choice == addIndex) {
            createRemote();
        } else {
            runRemote(remotes[choice]);
        }
    }
}
