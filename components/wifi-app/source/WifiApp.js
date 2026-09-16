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
// Settings keeps the phone's "When Device Sleeps": on a laptop, "Turn Wi-Fi
// Off" switches the radio off while the machine is suspended (see
// nm-connectionmanager). Help opens webOS Archive's copy of HP's help site,
// since help.palm.com is gone.
//
// The system menu opens this card with a "target": the network the user
// tapped, which is either a secured network to join or the joined one to show.
//

enyo.kind({
	name: "WifiApp",
	kind: "VFlexBox",

	// The pane's views, in order.
	VIEW_MAIN: 0,
	VIEW_SETTINGS: 1,
	VIEW_KNOWN: 2,

	LABEL_SLEEP_KEEP_ON: $L("Best for prolonging battery life in most cases."),
	LABEL_SLEEP_TURN_OFF: $L("May provide better battery life when connected to some Wi-Fi networks."),

	components: [
		{name: "profileList", kind: "PalmService", service: "palm://com.palm.wifi/", method: "getprofilelist",
			onResponse: "profileListReceived"},
		{name: "profileDelete", kind: "PalmService", service: "palm://com.palm.wifi/", method: "deleteprofile",
			onResponse: "profileDeleted"},
		{name: "sleepModeGet", kind: "PalmService", service: "palm://com.palm.connectionmanager/",
			method: "getWakeOnWiFiMode", onResponse: "sleepModeReceived"},
		{name: "sleepModeSet", kind: "PalmService", service: "palm://com.palm.connectionmanager/",
			method: "setWakeOnWiFiMode", onResponse: "sleepModeReceived"},

		{kind: "ApplicationEvents", onApplicationRelaunch: "applyTarget"},

		{kind: "Toolbar", className: "enyo-toolbar-light wifi-app-header", pack: "center", components: [
			{kind: "Spacer", flex: 1},
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
			{name: "pane", kind: "Pane", className: "wifi-app-pane", transitionKind: "enyo.transitions.Simple", components: [
				{kind: "VFlexBox", components: [
					{name: "caption", className: "wifi-app-note wifi-app-caption", showing: false},
					{name: "config", kind: "WiFiConfig", onViewChange: "configViewChanged", onBssChange: "accessPointChanged"},
					{name: "autoJoinNote", className: "wifi-app-note", showing: false,
						content: $L("Your device automatically connects to known networks.")}
				]},
				{kind: "VFlexBox", components: [
					{kind: "RowGroup", caption: $L("When Device Sleeps"), components: [
						{name: "sleepMode", kind: "ListSelector", onChange: "sleepModeChosen", items: [
							{caption: $L("Keep Wi-Fi On"), value: "enable"},
							{caption: $L("Turn Wi-Fi Off"), value: "disable"}
						]}
					]},
					{name: "sleepNote", className: "wifi-app-note"}
				]},
				{kind: "VFlexBox", components: [
					{name: "knownGroup", kind: "RowGroup", caption: $L("Known Networks"), showing: false, components: [
						{name: "knownList", kind: "VirtualRepeater", onSetupRow: "knownRow", components: [
							{name: "knownItem", kind: "SwipeableItem", layoutKind: "HFlexLayout", confirmRequired: true,
								confirmCaption: $L("Delete"), onConfirm: "forgetKnown", components: [
								{name: "knownName", flex: 1},
								{name: "knownSecurity"}
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
			{caption: $L("Settings"), onclick: "showSettings"},
			{caption: $L("Known Networks"), onclick: "showKnown"},
			{kind: "HelpMenu", target: "https://help.webosarchive.org/en-us/"}
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
		this.addressError = null;
		this.handleRefusals();
		this.applyTarget();
	},

	// --- what lib/wifi does not do when the service says no ----------------
	//
	// lib/wifi names handleConnectFailure, handleSetStateFailure and
	// handleDeleteProfileFailure as its services' onFailure handlers and never
	// defines them, and its connect handler reads only a profileId. A refusal
	// went nowhere: the Sign In button spun for good, and the radio switch
	// stayed disabled on a position the radio never took. The handlers are
	// given here, on this card's own instances, rather than in HP's library.

	handleRefusals() {
		const config = this.$.config;
		const answered = config.handleConnectResponse;
		config.handleConnectResponse = (inSender, inResponse, inRequest) => {
			if (inResponse && inResponse.returnValue === false && config.isInSecurityView())
				this.joinRefused(inResponse.errorText);
			return answered.call(config, inSender, inResponse, inRequest);
		};
		config.handleSetStateFailure = () => this.radioRefused();
		config.handleDeleteProfileFailure = () => {};
		config.$.wifiIpConfig.handleConnectFailure = (inSender, inResponse) =>
			this.addressesRefused(inResponse && inResponse.errorText);
	},

	joinRefused(text) {
		const config = this.$.config;
		config.$.joinMessage.setContent(text || $L("Unable to connect. Try again."));
		config.$.joinMessage.show();
		config.disableJoinButtons(false);
	},

	radioRefused() {
		const wanted = this.radioWanted;
		this.radioWanted = null;
		if (wanted !== null)
			this.$.radioSwitch.setState(!wanted);
		this.$.radioSwitch.setDisabled(false);
	},

	// Kept until the view changes: the access point's next update would
	// otherwise put the connected caption straight back over it.
	addressesRefused(text) {
		this.addressError = $L("The address settings were not applied: ") + (text || "");
		this.$.caption.setContent(this.addressError);
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
		this.addressError = null;
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
		// Shown while the radio is on even when it has nothing to say: the empty
		// line is part of the layout the list sits under.
		this.$.caption.setContent(caption);
		this.$.caption.setShowing(radioOn);

		if (this.radioWanted === radioOn)
			this.$.radioSwitch.setDisabled(false);

		if (inView === "NetworkList")
			this.showPendingTarget();
	},

	accessPointChanged(inSender, inInfo) {
		this.accessPoint = inInfo;
		if (this.$.config.isInIpConfigView() && !this.addressError)
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

	// --- the menu's other views ---------------------------------------------

	showView(index) {
		this.$.pane.selectViewByIndex(index);
		this.$.radioSwitch.hide();
		this.$.backButton.show();
	},

	showSettings() {
		this.showView(this.VIEW_SETTINGS);
		this.$.sleepModeGet.call({});
	},

	sleepModeChosen() {
		this.$.sleepModeSet.call({mode: this.$.sleepMode.getValue()});
	},

	// The mode in force, from either call. A refused change asks again, so the
	// list goes back to what the service holds instead of showing the choice
	// that was not taken.
	sleepModeReceived(inSender, inResponse) {
		const mode = inResponse && inResponse.mode;
		if (mode !== "enable" && mode !== "disable") {
			if (inSender === this.$.sleepModeSet)
				this.$.sleepModeGet.call({});
			return;
		}
		this.$.sleepMode.setValue(mode);
		this.$.sleepNote.setContent(mode === "enable" ? this.LABEL_SLEEP_KEEP_ON : this.LABEL_SLEEP_TURN_OFF);
	},

	// --- the known networks -------------------------------------------------

	showKnown() {
		this.showView(this.VIEW_KNOWN);
		this.$.profileList.call({});
	},

	showMain() {
		this.$.pane.selectViewByIndex(this.VIEW_MAIN);
		const config = this.$.config;
		this.$.radioSwitch.setShowing(config.isInOffView() || config.isInNetworkView());
		this.$.backButton.hide();
	},

	// "No known networks." is for a list that could not be read; an empty one
	// is an empty group, as on the phone.
	profileListReceived(inSender, inResponse) {
		const ok = inResponse && inResponse.returnValue === true && Array.isArray(inResponse.profileList);
		this.known = ok ? inResponse.profileList : [];
		this.$.knownGroup.setShowing(ok);
		this.$.noKnown.setShowing(!ok);
		if (ok)
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
