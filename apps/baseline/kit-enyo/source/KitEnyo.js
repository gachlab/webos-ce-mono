/* Copyright (c) 2026 webOS CE modern build. Licensed under the Apache License, Version 2.0. */

/*
 * The kit as enyo drew it: the same controls as sdk/ui-kit's showcase, in
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
 * shown in each of its states at once -- normal, pressed, disabled, on/off,
 * checked, held, selected, focused -- so a screenshot of it is complete and is
 * the yardstick every state of the rewritten kit is measured against.
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
					{kind: "Button", caption: "Pressed", className: "enyo-button enyo-button-depressed"},
					{kind: "Button", caption: "Dark", className: "enyo-button enyo-button-dark"},
					{kind: "Button", caption: "Affirmative", className: "enyo-button enyo-button-affirmative"},
					{kind: "Button", caption: "Negative", className: "enyo-button enyo-button-negative"},
					{kind: "Button", caption: "Blue", className: "enyo-button enyo-button-blue"},
					{kind: "Button", caption: "Gray", className: "enyo-button enyo-button-gray"},
					{kind: "Button", caption: "Disabled", disabled: true}
				]},

				{kind: "RowGroup", caption: "Toggles", components: [
					{kind: "Item", layoutKind: "HFlexLayout", align: "center", components: [
						{content: "On", flex: 1},
						{kind: "ToggleButton", state: true}
					]},
					{kind: "Item", layoutKind: "HFlexLayout", align: "center", components: [
						{content: "Off", flex: 1},
						{kind: "ToggleButton", state: false}
					]},
					{kind: "Item", layoutKind: "HFlexLayout", align: "center", components: [
						{content: "With its own words", flex: 1},
						{kind: "ToggleButton", state: true, onLabel: "Yes", offLabel: "No"}
					]},
					{kind: "Item", layoutKind: "HFlexLayout", align: "center", components: [
						{content: "Disabled on", flex: 1},
						{kind: "ToggleButton", state: true, disabled: true}
					]},
					{kind: "Item", layoutKind: "HFlexLayout", align: "center", components: [
						{content: "Disabled off", flex: 1},
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
					{kind: "Item", className: "enyo-held", content: "Held (pressed)"},
					{kind: "Item", className: "enyo-item-selected", content: "Selected"},
					{kind: "Item", className: "enyo-disabled", content: "Disabled"},
					{kind: "Item", layoutKind: "HFlexLayout", align: "center", components: [
						{content: "With something at the end", flex: 1},
						{kind: "ToggleButton", state: false}
					]}
				]},

				{kind: "RowGroup", caption: "Fields and checks", components: [
					{kind: "Input", hint: "Enter network name"},
					{kind: "Input", value: "Focused", className: "enyo-input enyo-input-focus"},
					{kind: "Input", value: "Disabled", disabled: true},
					{kind: "PasswordInput", hint: "Password"},
					{kind: "Item", layoutKind: "HFlexLayout", align: "center", components: [
						{content: "Checked", flex: 1},
						{kind: "CheckBox", checked: true}
					]},
					{kind: "Item", layoutKind: "HFlexLayout", align: "center", components: [
						{content: "Not checked", flex: 1},
						{kind: "CheckBox"}
					]},
					{kind: "Item", layoutKind: "HFlexLayout", align: "center", components: [
						{content: "Checked disabled", flex: 1},
						{kind: "CheckBox", checked: true, disabled: true}
					]},
					{kind: "Item", layoutKind: "HFlexLayout", align: "center", components: [
						{content: "Unchecked disabled", flex: 1},
						{kind: "CheckBox", disabled: true}
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
					]},
					{kind: "Item", components: [
						{kind: "RadioGroup", value: 0, components: [
							{caption: "Open", disabled: true},
							{caption: "WPA Personal", disabled: true}
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

				{kind: "RowGroup", caption: "Tabs across the top", components: [
					{kind: "TabGroup", value: 0, components: [
						{kind: "TabButton", caption: "All"},
						{kind: "TabButton", caption: "Contacts"},
						{kind: "TabButton", caption: "Content"},
						{kind: "TabButton", caption: "Actions"}
					]},
					{kind: "TabGroup", value: 0, components: [
						{kind: "TabButton", caption: "Enabled"},
						{kind: "TabButton", caption: "Disabled", disabled: true}
					]}
				]},

				{kind: "RowGroup", caption: "Icon buttons in a toolbar", components: [
					{kind: "Item", layoutKind: "HFlexLayout", align: "center", components: [
						{content: "A round picture button", flex: 1},
						{kind: "IconButton", caption: "Add"}
					]},
					{kind: "Item", layoutKind: "HFlexLayout", align: "center", components: [
						{content: "Disabled", flex: 1},
						{kind: "IconButton", caption: "Add", disabled: true}
					]}
				]},

				{kind: "Divider", caption: "Nearby"},
				{kind: "Item", content: "One under the captioned divider"},
				{kind: "AlphaDivider", caption: "S"},
				{kind: "Item", content: "Smith"},
				{kind: "Item", content: "Sullivan"},

				{kind: "RowGroup", caption: "A search field", components: [
					{kind: "SearchInput", hint: "Search"}
				]},

				{kind: "RowGroup", caption: "A field that grows", components: [
					{kind: "RichText", hint: "Type several lines; it grows to fit", richContent: false}
				]},

				{kind: "RowGroup", caption: "A picker", components: [
					{kind: "IntegerPicker", label: "Minutes", value: 30, min: 0, max: 59}
				]},

				{kind: "RowGroup", caption: "A date picker", components: [
					{kind: "DatePicker", label: "Date", minYear: 1900, maxYear: 2020}
				]},

				{kind: "RowGroup", caption: "A time picker", components: [
					{kind: "TimePicker", label: "Time (12-hour)", minuteInterval: 5},
					{kind: "TimePicker", label: "Time (24-hour)", minuteInterval: 5, is24HrMode: true}
				]},

				{kind: "RowGroup", caption: "A prev/next banner", components: [
					{kind: "PrevNextBanner", content: "Page 3 of 5"},
					{kind: "PrevNextBanner", content: "At the start", previousDisabled: true},
					{kind: "PrevNextBanner", content: "At the end", nextDisabled: true}
				]},

				{kind: "RowGroup", caption: "A folding section", components: [
					{kind: "DividerDrawer", caption: "Advanced", open: true, components: [
						{kind: "Item", content: "Hidden until the heading is tapped"},
						{kind: "Item", content: "Folds away again when it is tapped once more"}
					]}
				]},

				{kind: "RowGroup", caption: "A list that opens where it was tapped", components: [
					{kind: "Button", caption: "Open here", onclick: "openPopup"}
				]},

				{kind: "RowGroup", caption: "A toaster", components: [
					{kind: "Button", caption: "Show", onclick: "showToast"}
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

		{kind: "Toolbar", components: [
			{kind: "Button", caption: "Cancel"},
			{kind: "Button", caption: "Done", className: "enyo-button enyo-button-affirmative"}
		]},

		{name: "dialog", kind: "ModalDialog", caption: "Forget network?", components: [
			{content: "The device will stop joining it on its own.",
				className: "kit-enyo-dialog-message"},
			{kind: "Button", caption: "Forget", className: "enyo-button enyo-button-negative",
				onclick: "closeDialog"},
			{kind: "Button", caption: "Cancel", onclick: "closeDialog"}
		]},

		{name: "popup", kind: "PopupList", items: [
			{caption: "Open"},
			{caption: "Copy link"},
			{caption: "Share"}
		]},

		{name: "toaster", kind: "Toaster", flyInFrom: "bottom", components: [
			{content: "Saved"}
		]},

		{kind: "AppMenu", components: [
			{caption: "Settings"},
			{caption: "Known Networks"},
			{caption: "Disabled", disabled: true},
			{kind: "HelpMenu", target: "https://help.webosarchive.org/en-us/"}
		]}
	],

	openDialog: function() {
		this.$.dialog.openAtCenter();
	},

	closeDialog: function() {
		this.$.dialog.close();
	},

	openPopup: function(sender, event) {
		this.$.popup.openAtEvent(event);
	},

	showToast: function() {
		this.$.toaster.open();
	}
});
