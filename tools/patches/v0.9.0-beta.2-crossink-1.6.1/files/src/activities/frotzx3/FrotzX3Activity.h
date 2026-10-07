#pragma once

#include "activities/Activity.h"

class FrotzX3Activity final : public Activity {
 public:
  explicit FrotzX3Activity(GfxRenderer& renderer, MappedInputManager& mappedInput)
      : Activity("FrotzX3", renderer, mappedInput) {}

  void onEnter() override;
  void onExit() override;
  void loop() override;
  void render(RenderLock&&) override;

 private:
  enum class Menu {
    Main,
    Go,
    Take,
    Examine,
    Open,
    GameMenu,
    AdventureLog,
    Save,
    Load,
    Mailbox,
    Keyboard
  };

  enum class KeyboardMode {
    Groups,
    Letters,
    Suggestions
  };

  enum class SingleKeyScreen {
    Common,
    More,
    Letters,
    Symbols,
    Navigation,
    FunctionKeys
  };

  Menu currentMenu = Menu::Main;
  Menu menuBeforeKeyboard = Menu::Main;

  KeyboardMode keyboardMode = KeyboardMode::Groups;

  SingleKeyScreen singleKeyScreen = SingleKeyScreen::Common;
  int singleKeyIndex = 0;

  int selectedIndex = 0;
  int keyboardIndex = 0;
  int selectedGroup = 0;
  int suggestionIndex = 0;

  bool mailboxOpen = false;
  bool leafletTaken = false;

  const char* lastCommand = nullptr;
  const char* message = nullptr;

  char typedCommand[64] = {};

  void moveSelection(int delta);
  void activateSelection();
  void goBack();

  void openKeyboard();
  void moveKeyboard(int delta);
  void activateKeyboardKey();
  void submitTypedCommand();

  int getSingleKeyItemCount() const;
  void moveSingleKeySelection(int delta);
  void activateSingleKeySelection();
  void submitSingleKeyValue(int key);

  int getSuggestionCount() const;
  const char* getSuggestion(int index) const;
  void acceptSuggestion();
};
