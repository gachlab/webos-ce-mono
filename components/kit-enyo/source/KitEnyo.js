/* Copyright (c) 2026 webOS CE modern build. Licensed under the Apache License, Version 2.0. */

/*
 * The kit as enyo drew it: the same controls as components/cards' showcase, in
 * the same order and with the same captions, built out of HP's own kinds and
 * its Onyx theme.
 *
 * It is a measuring stick, not a card anybody uses. The rewritten cards are
 * meant to look like the ones they replace, and "look like" is a thing you can
 * only argue about until the two are side by side; this one is the other side.
 * Put it next to com.gachlab.app.kit, take both screenshots, and the differences
 * are numbers rather than opinions.
 *
 * It talks to no service and remembers nothing on purpose: every control is
 * shown in each of its states at once, so a screenshot of it is complete.
 */

enyo.kind({
	name: "KitEnyo",
	kind: enyo.VFlexBox,

	components: [
		{kind: "Header", className: "enyo-header-dark kit-enyo-header", components: [
			{content: "Kit (enyo)", flex: 1, className: "kit-enyo-title"}
		]},

		{name: "scroller", kind: "Scroller", flex: 1, components: [
			{className: "kit-enyo-column", components: [

				{className: "kit-enyo-note", content:
					"Every control, in each state, as enyo draws it. This card is the "
					+ "reference the rewritten kit is measured against."},

				{kind: "RowGroup", caption: "Buttons", components: [
					{kind: "Button", caption: "Plain"},
					{kind: "Button", caption: "Dark", className: "enyo-button enyo-button-dark"},
					{kind: "Button", caption: "Affirmative", className: "enyo-button enyo-button-affirmative"},
					{kind: "Button", caption: "Negative", className: "enyo-button enyo-button-negative"},
					{kind: "Button", caption: "Blue", className: "enyo-button enyo-button-blue"},
					{kind: "Button", caption: "Gray", className: "enyo-button enyo-button-gray"},
					{kind: "Button", caption: "Disabled", disabled: true}
				]},

				{kind: "RowGroup", caption: "Toggles", components: [
					{kind: "Item", layoutKind: "HFlexLayout", align: "center", components: [
						{content: "Answers with an event", flex: 1},
						{kind: "ToggleButton", state: true}
					]},
					{kind: "Item", layoutKind: "HFlexLayout", align: "center", components: [
						{content: "With its own words", flex: 1},
						{kind: "ToggleButton", state: true, onLabel: "Yes", offLabel: "No"}
					]},
					{kind: "Item", layoutKind: "HFlexLayout", align: "center", components: [
						{content: "Disabled", flex: 1},
						{kind: "ToggleButton", state: false, disabled: true}
					]}
				]},

				{kind: "RowGroup", caption: "Rows", components: [
					{kind: "Item", content: "One line"},
					{kind: "Item", components: [
						{content: "Two lines"},
						{className: "kit-enyo-detail", content: "and what the second one says"}
					]},
					{kind: "Item", components: [
						{className: "kit-enyo-strong", content: "The one that matters"},
						{className: "kit-enyo-detail", content: "CONNECTING..."}
					]},
					{kind: "Item", layoutKind: "HFlexLayout", align: "center", components: [
						{content: "With something at the end", flex: 1},
						{kind: "ToggleButton", state: false}
					]}
				]},

				{kind: "RowGroup", caption: "Fields and checks", components: [
					{kind: "Input", hint: "Enter network name"},
					{kind: "PasswordInput", hint: "Password"},
					{kind: "Item", layoutKind: "HFlexLayout", align: "center", components: [
						{content: "Checked", flex: 1},
						{kind: "CheckBox", checked: true}
					]},
					{kind: "Item", layoutKind: "HFlexLayout", align: "center", components: [
						{content: "Not checked", flex: 1},
						{kind: "CheckBox"}
					]}
				]},

				{kind: "RowGroup", caption: "Choosing one of several", components: [
					{kind: "ListSelector", value: "ask", items: [
						{caption: "Never", value: "off"},
						{caption: "Always ask", value: "ask"},
						{caption: "Automatically", value: "auto"}
					]}
				]},

				{kind: "RowGroup", caption: "Swipe to delete", components: [
					{kind: "SwipeableItem", confirmRequired: true, confirmCaption: "Delete",
						layoutKind: "HFlexLayout", components: [
						{content: "GachWLAN", flex: 1},
						{content: "WPA Personal"}
					]},
					{kind: "SwipeableItem", confirmRequired: true, confirmCaption: "Delete",
						layoutKind: "HFlexLayout", components: [
						{content: "ABACANTVWIFID62D", flex: 1},
						{content: "WPA Personal"}
					]}
				]},

				{kind: "RowGroup", caption: "One of a few", components: [
					{kind: "Item", components: [
						{kind: "RadioGroup", value: 0, components: [
							{caption: "Open"},
							{caption: "WPA Personal"},
							{caption: "WEP"}
						]}
					]}
				]},

				{kind: "RowGroup", caption: "A button that is working", components: [
					{kind: "ActivityButton", caption: "Joining...", active: true,
						className: "enyo-button enyo-button-dark"},
					{kind: "ActivityButton", caption: "Saving...", active: true}
				]},

				{kind: "RowGroup", caption: "Sliders", components: [
					{kind: "Item", components: [
						{kind: "Slider", position: 60}
					]}
				]},

				{kind: "RowGroup", caption: "Waiting and failing", components: [
					{kind: "Item", layoutKind: "HFlexLayout", align: "center", components: [
						{content: "Searching for networks...", flex: 1},
						{kind: "Spinner", showing: true}
					]},
					{kind: "Item", components: [
						{kind: "ProgressBar", position: 40}
					]}
				]},
				{className: "kit-enyo-error", content: "Incorrect password"},

				{kind: "RowGroup", caption: "Asking before doing", components: [
					{kind: "Button", caption: "Open", onclick: "openDialog"}
				]}
			]}
		]},

		{name: "dialog", kind: "ModalDialog", caption: "Forget network?", components: [
			{content: "The device will stop joining it on its own.",
				className: "kit-enyo-dialog-message"},
			{kind: "Button", caption: "Forget", className: "enyo-button enyo-button-negative",
				onclick: "closeDialog"},
			{kind: "Button", caption: "Cancel", onclick: "closeDialog"}
		]},

		{kind: "AppMenu", components: [
			{caption: "Settings"},
			{caption: "Known Networks"},
			{kind: "HelpMenu", target: "https://help.webosarchive.org/en-us/"}
		]}
	],

	openDialog: function() {
		this.$.dialog.openAtCenter();
	},

	closeDialog: function() {
		this.$.dialog.close();
	}
});
