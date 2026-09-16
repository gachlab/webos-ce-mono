/* @@@LICENSE
*
*      Copyright (c) 2010-2012 Hewlett-Packard Development Company, L.P.
*
* Licensed under the Apache License, Version 2.0 (the "License");
* you may not use this file except in compliance with the License.
* You may obtain a copy of the License at
*
* http://www.apache.org/licenses/LICENSE-2.0
*
* Unless required by applicable law or agreed to in writing, software
* distributed under the License is distributed on an "AS IS" BASIS,
* WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
* See the License for the specific language governing permissions and
* limitations under the License.
*
* LICENSE@@@ */




#ifndef SYSTEMMENU_H
#define SYSTEMMENU_H


#include <QGraphicsObject>
#if (QT_VERSION >= QT_VERSION_CHECK(5, 0, 0))
#include <QQuickPaintedItem>
#endif
#include "QmlItem.h"
#include "QmlSceneItem.h"
#include <QPropertyAnimation>
#include <QTimer>
#include "StatusBarServicesConnector.h"

#if (QT_VERSION < QT_VERSION_CHECK(5, 0, 0))
class QDeclarativeEngine;
class QDeclarativeComponent;
#else
class QQmlEngine;
class QQmlComponent;
#endif
class MenuHandler;

class SystemMenu : public QGraphicsObject
{
	Q_OBJECT

public:
	SystemMenu(int width, int height, bool restricted = false);
	~SystemMenu();

	void init();

    void setOpened(bool opened);
    bool isOpened() { return m_opened; }

	QRectF boundingRect() const;  // This item is Left Aligned (The position  of the icon is the position of the LEFT EDGE of the bounding rect)
	void paint(QPainter* painter, const QStyleOptionGraphicsItem* option, QWidget* widget);
	int getRightEdgeOffset() { return m_rightEdgeOffset; }

private Q_SLOTS:
	void slotCloseSystemMenu();

	void slotWifiMenuOpened();
	void slotWifiMenuClosed();
	void slotWifiOnOffTriggered();
	void slotWifiPrefsTriggered();
	void slotWifiNetworkSelected(int index, QString name, int profileId, QString securityType, QString connStatus);

	// The cable. Ours: webOS had no wired networking in this menu.
	void slotWiredToggleTriggered(bool connected);
	void slotWiredStateChanged(bool connected, std::string interfaceName, std::string ipAddress);

	void slotBluetoothMenuOpened();
	void slotBluetoothMenuClosed();
	void slotBluetoothOnOffTriggered();
	void slotBluetoothPrefsTriggered();
	void slotBluetoothDeviceSelected(int index);

	void slotVpnMenuOpened();
	void slotVpnMenuClosed();
	void slotVpnPrefsTriggered();
	void slotVpnNetworkSelected(QString name, QString status, QString profInfo);

	void slotAirplaneModeTriggered();
	void slotRotationLockTriggered(bool isLocked);
	void slotMuteToggleTriggered(bool isMuted);
    void slotRotationLockChanged(OrientationEvent::Orientation rotationLock);
	void slotMuteSoundChanged(bool muteOn);
	void slotDisplayMaxBrightnessChanged(int brightness);

	void slotPowerdConnectionStateChanged(bool connected);
	void slotBatteryLevelUpdated(int percentage);

	void slotWifiStateChanged(bool wifiOn, bool wifiConnected, std::string wifiSSID, std::string wifiConnState);
	void slotWifiAvailableNetworksListUpdate(int numNetworks, t_wifiAccessPoint* list);

	void slotBluetoothTurnedOn();
	void slotBluetoothPowerStateChanged(t_radioState radioState);
	void slotBluetoothConnStateChanged(bool btConnected, std::string deviceName);
	void slotBluetoothTrustedDevicesUpdate(int numTrustedDevices, t_bluetoothDevice* list);
	void slotBluetoothParedDevicesAvailable(bool available);
	void slotBluetoothUpdateDeviceStatus(t_bluetoothDevice* deviceStatus);
	void slotVpnProfileListUpdate(int numProfiles, t_vpnProfile* list);
	void slotVpnStateChanged(bool enabled);
	void slotAirplaneModeState(t_airplaneModeState state);
	void slotMenuBrightnessChanged(qreal value, bool save);

	void slotSystemTimeChanged();
	void slotPositiveSpaceChangeFinished(QRect rect);
 	void tick();

 	Q_SIGNALS:
	void signalCloseMenu();

private:

	bool sceneEvent(QEvent* event);

	void setAirplaneModeState(t_airplaneModeState state);
	void launchApp(std::string appId, std::string params);

	void connectBtAudioDevice(std::string address, unsigned int cod);

    QRect m_bounds;
	bool m_restricted;
	bool m_opened;
	int m_rightEdgeOffset;
#if (QT_VERSION < QT_VERSION_CHECK(5, 0, 0))
    QDeclarativeComponent* m_qmlMenu;
#else
    QQmlComponent* m_qmlMenu;
#endif
	// Top Level Menu Object
	QmlItem* m_menuObject;
#if (QT_VERSION >= QT_VERSION_CHECK(5, 0, 0))
	QmlSceneItem* m_menuSurface = 0;
#endif

	// Children Menu Objects
	QmlItem* m_wifiMenu;
	QmlItem* m_vpnMenu;
	QmlItem* m_bluetoothMenu;

	bool m_wifiMenuOpened;
	bool m_wifiOn;

	bool m_bluetoothMenuOpened;
	t_radioState m_bluetoothPower;
	bool m_btPairedDevicesAvailable;
	bool m_btTurnOnRequested;
	std::vector<t_bluetoothDevice> m_trustedDevices;
	std::string m_pendingDevAddress;
	int         m_pendingCod;

	QTimer *m_dateTimer;

	bool m_vpnMenuOpened;

	MenuHandler* m_menuHandler; // provides an interface to interact with the QML menu

	t_airplaneModeState m_airplaneModeState;

	struct {
		int year;
		int month;
		int day;
	} m_lastUpdateDate;
};



 class MenuHandler : public QObject
 {
    Q_OBJECT
  public:
    MenuHandler(SystemMenu *parent = 0);
    ~MenuHandler();

    void updateBatteryLevel(QString level) { Q_EMIT batteryLevelUpdated(level); }
    void signalWifiListUpdated() { Q_EMIT wifiListUpdated(); }

Q_SIGNALS:

	void batteryLevelUpdated(QString batteryLevel);
	void wifiStateChanged(bool wifiOn, QString wifiString);
	void wifiListUpdated();

 private:

	SystemMenu* m_systemMenu;
 };



// Registered into QML as SystemMenu.AnimatedSpinner. Under QML 1 a plain
// QGraphicsObject could be instantiated inside a .qml file, because QML 1 items
// were themselves QGraphicsObjects. A QtQuick 2 scene has no place for one:
// visual QML types must derive from QQuickItem. QQuickPaintedItem is the
// QPainter-backed one, which is exactly what this spinner needs -- it rotates a
// pixmap -- so the body of paint() is unchanged.
#if (QT_VERSION < QT_VERSION_CHECK(5, 0, 0))
 class AnimatedSpinner : public QGraphicsObject
#else
 class AnimatedSpinner : public QQuickPaintedItem
#endif
  {
      Q_OBJECT
      Q_PROPERTY(int duration READ duration WRITE setDuration)
      Q_PROPERTY(bool on READ on WRITE setOn)
      Q_PROPERTY(int currentFrame READ currentFrame WRITE setCurrentFrame)
  public:
#if (QT_VERSION < QT_VERSION_CHECK(5, 0, 0))
      AnimatedSpinner(QObject *parent = 0);
#else
      AnimatedSpinner(QQuickItem *parent = 0);
#endif
      ~AnimatedSpinner();

      int duration() const { return m_duration; }
      void setDuration(const int duration );

      int currentFrame() const { return m_currentFrame; }
      void setCurrentFrame(const int frame );

      int on() const { return m_on; }
      void setOn(const bool on );

#if (QT_VERSION < QT_VERSION_CHECK(5, 0, 0))
  	QRectF boundingRect() const;
  	void paint(QPainter* painter, const QStyleOptionGraphicsItem* option, QWidget* widget);
#else
  	// QQuickItem already provides boundingRect() from width()/height(), which
  	// the constructor sets from the pixmap.
  	void paint(QPainter* painter);
#endif

  private:
    int m_duration;
    bool m_on;
    QPixmap m_img;
    QRectF m_bounds;
    int m_nFrames;
    int m_currentFrame;

    QPropertyAnimation m_anim;
  };



#endif /* SYSTEMMENU_H */
