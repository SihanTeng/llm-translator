// UNVERIFIED: cannot be compiled or tested on GNOME (LayerShellQt is
// unavailable there). Build with -DTRANSLATOR_WITH_LAYERSHELL=ON on a KDE
// Plasma 6 or wlroots session to verify.

#include "LayerShellPopup.h"

#include <LayerShellQt/Window>

#include <QGuiApplication>
#include <QMargins>
#include <QScreen>
#include <QWindow>
#include <QWidget>

bool LayerShellPopup::isSupported()
{
    return QGuiApplication::platformName() == QLatin1String("wayland")
        && LayerShellQt::Window::isSupported();
}

void LayerShellPopup::placeAt(QWidget *window, const QPoint &globalPos)
{
    QScreen *screen = QGuiApplication::screenAt(globalPos);
    if (!screen)
        screen = QGuiApplication::primaryScreen();

    window->createWinId();
    QWindow *handle = window->windowHandle();
    handle->setScreen(screen);

    LayerShellQt::Window *layerShell = LayerShellQt::Window::get(handle);
    layerShell->setLayer(LayerShellQt::Window::LayerOverlay);
    layerShell->setKeyboardInteractivity(LayerShellQt::Window::KeyboardInteractivityNone);
    layerShell->setAnchors(LayerShellQt::Window::Anchors{
        LayerShellQt::Window::AnchorTop | LayerShellQt::Window::AnchorLeft});

    const QPoint origin = screen->geometry().topLeft();
    layerShell->setMargins(QMargins(globalPos.x() - origin.x(),
                                    globalPos.y() - origin.y(), 0, 0));

    window->show();
}
