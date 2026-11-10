#pragma once
#include "models.h"
class QWidget;
namespace extras {
void loadLibraries(const QString& assets);
void editLibrary(QWidget* parent, const QString& assets, bool quota);
bool insertNode(QWidget* parent, Element& element);
bool runChecks(QString* error);
}
