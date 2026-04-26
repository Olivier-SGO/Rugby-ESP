#pragma once
#include "MatchDB.h"
#include "DisplayManager.h"
#include "SceneManager.h"

class WebUI {
public:
    void begin(MatchDB* db);
    void handle();
    bool shouldRestart() const { return _restartPending; }
private:
    bool _restartPending = false;
};

extern WebUI Web;
