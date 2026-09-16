/* @@@LICENSE
 *
 * Copyright (c) 2026 webOS CE modern build
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 * LICENSE@@@ */

//
// The Wi-Fi settings card, com.palm.app.wifi.
//
// HP's card of the same id was never released as source, so this one is ours.
// It is built to be the same card to the user: a light header with the Wi-Fi
// icon and a radio switch, the network list, the join screens and the
// connected-network screen of enyo's WiFiConfig (lib/wifi, which HP did
// release), and a list of known networks behind the app menu, each removable
// with a swipe.
//
// Two things the phone's card had are not here, both because a laptop has
// nothing behind them: the "When Device Sleeps" setting (com.palm.connection-
// manager's wake-on-wifi mode) and the help link, whose site no longer exists.
//
// The system menu opens this card with a "target": the network the user
// tapped, which is either a secured network to join or the joined one to show.
//

enyo.kind({
	name: "WifiApp",
	kind: "VFlexBox",

	// The pane's views, in order.
	VIEW_MAIN: 0,
	VIEW_KNOWN: 1,

	components: [
		{name: "profileList", kind: "PalmService", service: "palm://com.palm.wifi/", method: "getprofilelist",
			onResponse: "profileListReceived"},
		{name: "profileDelete", kind: "PalmService", service: "palm://com.palm.wifi/", method: "deleteprofile",
			onResponse: "profileDeleted"},

		{kind: "ApplicationEvents", onApplicationRelaunch: "applyTarget"},

		{kind: "Toolbar", className: "enyo-toolbar-light wifi-app-header", pack: "center", components: [
			{flex: 1},
			{kind: "HFlexBox", align: "center", components: [
				{className: "wifi-app-header-icon"},
				{name: "title", className: "wifi-app-title", content: $L("Wi-Fi")}
			]},
			{flex: 1, components: [
				{name: "radioSwitch", kind: "ToggleButton", className: "wifi-app-radio-switch", showing: false,
					onChange: "radioSwitched"}
			]}
		]},
		{className: "wifi-app-header-shadow"},

		{kind: "Scroller", flex: 1, components: [
			{name: "pane", kind: "Pane", className: "wifi-app-column", transitionKind: "enyo.transitions.Simple", components: [
				{kind: "VFlexBox", components: [
					{name: "caption", className: "wifi-app-note wifi-app-caption", showing: false},
					{name: "config", kind: "WiFiConfig", onViewChange: "configViewChanged", onBssChange: "accessPointChanged"},
					{name: "autoJoinNote", className: "wifi-app-note", showing: false,
						content: $L("Your device automatically connects to known networks.")}
				]},
				{kind: "VFlexBox", components: [
					{name: "knownGroup", kind: "RowGroup", caption: $L("Known Networks"), showing: false, components: [
						{name: "knownList", kind: "VirtualRepeater", onSetupRow: "knownRow", components: [
							{name: "knownItem", kind: "SwipeableItem", layoutKind: "HFlexLayout", confirmRequired: true,
								confirmCaption: $L("Delete"), onConfirm: "forgetKnown", components: [
								{name: "knownName", flex: 1, className: "wifi-app-known-name"},
								{name: "knownSecurity", className: "wifi-app-known-security"}
							]}
						]}
					]},
					{name: "noKnown", className: "wifi-app-note", showing: false, content: $L("No known networks.")}
				]}
			]}
		]},

		{name: "backButton", kind: "Button", className: "wifi-app-column wifi-app-back", showing: false,
			caption: $L("Back"), onclick: "showMain"},

		{kind: "AppMenu", components: [
			{caption: $L("Known Networks"), onclick: "showKnown"}
		]}
	],

	create() {
		this.inherited(arguments);
		this.known = [];
		this.accessPoint = null;
		// What the user last asked the radio to be, so the switch stays
		// disabled until the radio has caught up with it.
		this.radioWanted = null;
		// A target that names the joined network can only be shown once
		// WiFiConfig knows which network that is; it waits here until then.
		this.pendingTarget = null;
		this.applyTarget();
	},

	// --- the target the system menu sent -----------------------------------

	applyTarget() {
		const target = enyo.windowParams && enyo.windowParams.target;
		if (!target || target.ssid === undefined)
			return;
		const joined = target.connectState === "ipConfigured" || target.connectState === "ipFailed";
		if (joined) {
			this.pendingTarget = target;
			this.showPendingTarget();
		} else if (target.securityType) {
			this.$.config.showJoinSecureNetwork(target);
		}
	},

	showPendingTarget() {
		const target = this.pendingTarget;
		const joined = this.$.config.getJoinedNetwork();
		if (!target || !joined)
			return;
		this.pendingTarget = null;
		this.$.config.retrieveIpInfo({profileId: target.profileId || joined.profileId});
	},

	// --- the header and the caption, per WiFiConfig view --------------------

	configViewChanged(inSender, inView) {
		const radioOn = inView !== "Off";
		const onMain = this.$.pane.getViewIndex() === this.VIEW_MAIN;
		const listLike = inView === "Off" || inView === "NetworkList" || inView === "NoInternet";

		this.$.radioSwitch.setState(radioOn);
		this.$.radioSwitch.setShowing(listLike && onMain);
		this.$.autoJoinNote.setShowing(radioOn && listLike);

		let caption = "";
		switch (inView) {
		case "IpConfig":
			caption = this.connectedCaption();
			break;
		case "JoinSecureNetwork": {
			const selected = this.$.config.getSelectedNetwork();
			caption = $L("Join ") + (selected ? selected.ssid : "");
			break;
		}
		case "JoinNewNetwork":
			caption = $L("Join Other Network");
			break;
		}
		this.$.caption.setContent(caption);
		this.$.caption.setShowing(radioOn && caption !== "");

		if (this.radioWanted === radioOn)
			this.$.radioSwitch.setDisabled(false);

		if (inView === "NetworkList")
			this.showPendingTarget();
	},

	accessPointChanged(inSender, inInfo) {
		this.accessPoint = inInfo;
		if (this.$.config.isInIpConfigView())
			this.$.caption.setContent(this.connectedCaption());
	},

	// "Connected to Home. BSSID 00:11:22:33:44:55, Channel 6." -- the BSSID and
	// channel only once the service has said which access point it is.
	connectedCaption() {
		const joined = this.$.config.getJoinedNetwork();
		let text = $L("Connected to ") + (joined ? joined.ssid : "") + ".";
		const ap = this.accessPoint;
		if (ap && ap.bssid)
			text += $L(" BSSID ") + ap.bssid + $L(", Channel ") + ap.channel + ".";
		return text;
	},

	radioSwitched(inSender, inOn) {
		if (inOn)
			this.$.config.turnWiFiOn();
		else
			this.$.config.turnWiFiOff();
		this.radioWanted = inOn;
		this.$.radioSwitch.setDisabled(true);
	},

	// --- the known networks -------------------------------------------------

	showKnown() {
		this.$.pane.selectViewByIndex(this.VIEW_KNOWN);
		this.$.radioSwitch.hide();
		this.$.backButton.show();
		this.$.profileList.call({});
	},

	showMain() {
		this.$.pane.selectViewByIndex(this.VIEW_MAIN);
		const config = this.$.config;
		this.$.radioSwitch.setShowing(config.isInOffView() || config.isInNetworkView());
		this.$.backButton.hide();
	},

	profileListReceived(inSender, inResponse) {
		const ok = inResponse && inResponse.returnValue === true && Array.isArray(inResponse.profileList);
		this.known = ok ? inResponse.profileList : [];
		const any = this.known.length > 0;
		this.$.knownGroup.setShowing(any);
		this.$.noKnown.setShowing(!any);
		if (any)
			this.$.knownList.render();
	},

	knownRow(inSender, inIndex) {
		const record = this.known[inIndex];
		if (!record)
			return false;
		const profile = record.wifiProfile;
		this.$.knownName.setContent(profile.ssid);
		this.$.knownSecurity.setContent(this.securityLabel(profile.security));

		const item = this.$.knownItem;
		const last = this.known.length - 1;
		if (last === 0)
			item.addClass("enyo-single");
		else if (inIndex === 0)
			item.addClass("enyo-first");
		else if (inIndex === last)
			item.addClass("enyo-last");
		return true;
	},

	securityLabel(security) {
		if (!security)
			return $L("Open");
		switch (security.securityType) {
		case "wep":          return $L("WEP");
		case "wpa-personal": return $L("WPA Personal");
		case "enterprise":   return $L("Enterprise");
		default:             return "";
		}
	},

	forgetKnown(inSender, inIndex) {
		const record = this.known[inIndex];
		if (record)
			this.$.profileDelete.call({profileId: record.wifiProfile.profileId});
	},

	// The list is read again after the delete has happened, not alongside it.
	profileDeleted() {
		if (this.$.pane.getViewIndex() === this.VIEW_KNOWN)
			this.$.profileList.call({});
	}
});
