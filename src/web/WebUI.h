#pragma once
#include "MatchDB.h"
#include "DisplayManager.h"
#include "SceneManager.h"

class WebUI {
public:
    void begin(MatchDB* db);
    void handle();
};

extern WebUI Web;
