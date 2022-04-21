#include <qt/raptoreum.h>

#include <QCoreApplication>

#include <functional>
#include <string>

extern const std::function<std::string(const char*)> G_TRANSLATION_FUN = [](const char* psz) {
  return QCoreApplication::translate("raptoreum-core", psz).toStdString();
};

int main(int argc, char* argv[]) { return GuiMain(argc, argv); }
