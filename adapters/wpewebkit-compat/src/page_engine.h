// Shared by page.cpp / frame.cpp / input.cpp — not a public header.
#pragma once

#include "wpewebkit_compat.h"
#include "wpe_webcontent.h"

#include <QTimer>

#include <memory>

struct QWebPage::Engine {
    std::unique_ptr<wpe_webcontent::HeadlessView> view;
    QTimer* glibPump = nullptr;
};
