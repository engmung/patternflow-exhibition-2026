{
	"patcher": {
		"fileversion": 1,
		"appversion": {
			"major": 8,
			"minor": 6,
			"revision": 0,
			"architecture": "x64",
			"modernui": 1
		},
		"classnamespace": "box",
		"rect": [59.0, 106.0, 1000.0, 560.0],
		"bglocked": 0,
		"openinpresentation": 1,
		"default_fontsize": 12.0,
		"default_fontface": 0,
		"default_fontname": "Arial",
		"gridonopen": 1,
		"gridsize": [15.0, 15.0],
		"gridsnaponopen": 1,
		"objectsnaponopen": 1,
		"statusbarvisible": 2,
		"toolbarvisible": 1,
		"boxes": [
			{
				"box": {
					"id": "obj-1",
					"maxclass": "comment",
					"fontface": 1,
					"numinlets": 1,
					"numoutlets": 0,
					"patching_rect": [24.0, 16.0, 200.0, 20.0],
					"presentation": 1,
					"presentation_rect": [4.0, 2.0, 200.0, 18.0],
					"text": "PF EXHIBIT SCENES"
				}
			},
			{
				"box": {
					"id": "obj-40",
					"maxclass": "comment",
					"fontsize": 10.0,
					"linecount": 6,
					"numinlets": 1,
					"numoutlets": 0,
					"patching_rect": [240.0, 10.0, 560.0, 78.0],
					"text": "Program Change n (panel pattern n) fires Live scene n + offset. Scenes are counted like Program Change: the top scene is 0. Alt CC reads CC 118 (FlowLocal's program mirror, sent on channel 16) as a program, on any channel: Live hands a track's MIDI to devices merged onto channel 1. After load and on Resync the js sends the resync request BF 77 7F (CC 119 = 127, channel 16) to midiout: keep this device alone on a MIDI track with MIDI To = Patternflow Exhibit, Ch. 16. The gate (Thru) is closed unless the Thru toggle is on. deferlow keeps the byte order into js."
				}
			},
			{
				"box": {
					"id": "obj-2",
					"maxclass": "newobj",
					"numinlets": 1,
					"numoutlets": 1,
					"outlettype": ["int"],
					"patching_rect": [24.0, 60.0, 50.0, 22.0],
					"text": "midiin"
				}
			},
			{
				"box": {
					"id": "obj-41",
					"maxclass": "newobj",
					"numinlets": 2,
					"numoutlets": 1,
					"outlettype": [""],
					"patching_rect": [24.0, 400.0, 50.0, 22.0],
					"text": "gate 1"
				}
			},
			{
				"box": {
					"id": "obj-3",
					"maxclass": "newobj",
					"numinlets": 1,
					"numoutlets": 0,
					"patching_rect": [24.0, 440.0, 56.0, 22.0],
					"text": "midiout"
				}
			},
			{
				"box": {
					"id": "obj-4",
					"maxclass": "newobj",
					"numinlets": 1,
					"numoutlets": 4,
					"outlettype": ["", "", "", ""],
					"patching_rect": [140.0, 340.0, 200.0, 22.0],
					"text": "js pf_exhibit_scenes.js",
					"varname": "pf_exhibit_scenes"
				}
			},
			{
				"box": {
					"id": "obj-5",
					"maxclass": "newobj",
					"numinlets": 1,
					"numoutlets": 3,
					"outlettype": ["bang", "int", "int"],
					"patching_rect": [140.0, 60.0, 100.0, 22.0],
					"text": "live.thisdevice"
				}
			},
			{
				"box": {
					"id": "obj-6",
					"maxclass": "message",
					"numinlets": 2,
					"numoutlets": 1,
					"outlettype": [""],
					"patching_rect": [140.0, 124.0, 40.0, 22.0],
					"text": "init"
				}
			},
			{
				"box": {
					"id": "obj-7",
					"maxclass": "newobj",
					"numinlets": 1,
					"numoutlets": 3,
					"outlettype": ["bang", "bang", "bang"],
					"patching_rect": [140.0, 92.0, 60.0, 22.0],
					"text": "t b b b"
				}
			},
			{
				"box": {
					"id": "obj-8",
					"maxclass": "message",
					"numinlets": 2,
					"numoutlets": 1,
					"outlettype": [""],
					"patching_rect": [270.0, 124.0, 130.0, 22.0],
					"text": "set waiting for Live"
				}
			},
			{
				"box": {
					"id": "obj-9",
					"maxclass": "message",
					"numinlets": 2,
					"numoutlets": 1,
					"outlettype": [""],
					"patching_rect": [270.0, 152.0, 130.0, 22.0],
					"text": "set no program yet"
				}
			},
			{
				"box": {
					"id": "obj-35",
					"maxclass": "newobj",
					"numinlets": 1,
					"numoutlets": 1,
					"outlettype": [""],
					"patching_rect": [80.0, 300.0, 60.0, 22.0],
					"text": "deferlow"
				}
			},
			{
				"box": {
					"id": "obj-34",
					"maxclass": "comment",
					"fontsize": 10.0,
					"numinlets": 1,
					"numoutlets": 0,
					"patching_rect": [140.0, 190.0, 240.0, 18.0],
					"text": "testing in the Max editor:"
				}
			},
			{
				"box": {
					"id": "obj-30",
					"maxclass": "message",
					"numinlets": 2,
					"numoutlets": 1,
					"outlettype": [""],
					"patching_rect": [140.0, 212.0, 46.0, 22.0],
					"text": "test 0"
				}
			},
			{
				"box": {
					"id": "obj-31",
					"maxclass": "message",
					"numinlets": 2,
					"numoutlets": 1,
					"outlettype": [""],
					"patching_rect": [192.0, 212.0, 46.0, 22.0],
					"text": "test 1"
				}
			},
			{
				"box": {
					"id": "obj-32",
					"maxclass": "message",
					"numinlets": 2,
					"numoutlets": 1,
					"outlettype": [""],
					"patching_rect": [244.0, 212.0, 54.0, 22.0],
					"text": "gostart"
				}
			},
			{
				"box": {
					"id": "obj-33",
					"maxclass": "message",
					"numinlets": 2,
					"numoutlets": 1,
					"outlettype": [""],
					"patching_rect": [304.0, 212.0, 42.0, 22.0],
					"text": "dump"
				}
			},
			{
				"box": {
					"id": "obj-10",
					"maxclass": "live.toggle",
					"numinlets": 1,
					"numoutlets": 1,
					"outlettype": [""],
					"parameter_enable": 1,
					"patching_rect": [420.0, 100.0, 15.0, 15.0],
					"presentation": 1,
					"presentation_rect": [4.0, 24.0, 15.0, 15.0],
					"saved_attribute_attributes": {
						"valueof": {
							"parameter_enum": ["off", "on"],
							"parameter_initial": [1],
							"parameter_initial_enable": 1,
							"parameter_invisible": 1,
							"parameter_longname": "Enable",
							"parameter_mmax": 1,
							"parameter_shortname": "Enable",
							"parameter_type": 2
						}
					},
					"varname": "Enable"
				}
			},
			{
				"box": {
					"id": "obj-11",
					"maxclass": "comment",
					"numinlets": 1,
					"numoutlets": 0,
					"patching_rect": [440.0, 98.0, 60.0, 20.0],
					"presentation": 1,
					"presentation_rect": [22.0, 22.0, 200.0, 18.0],
					"text": "Enable"
				}
			},
			{
				"box": {
					"id": "obj-12",
					"maxclass": "newobj",
					"numinlets": 1,
					"numoutlets": 1,
					"outlettype": [""],
					"patching_rect": [420.0, 130.0, 96.0, 22.0],
					"text": "prepend enable"
				}
			},
			{
				"box": {
					"id": "obj-13",
					"maxclass": "live.toggle",
					"numinlets": 1,
					"numoutlets": 1,
					"outlettype": [""],
					"parameter_enable": 1,
					"patching_rect": [560.0, 100.0, 15.0, 15.0],
					"presentation": 1,
					"presentation_rect": [4.0, 42.0, 15.0, 15.0],
					"saved_attribute_attributes": {
						"valueof": {
							"parameter_enum": ["off", "on"],
							"parameter_initial": [0],
							"parameter_initial_enable": 1,
							"parameter_invisible": 1,
							"parameter_longname": "Autostart",
							"parameter_mmax": 1,
							"parameter_shortname": "Autostart",
							"parameter_type": 2
						}
					},
					"varname": "Autostart"
				}
			},
			{
				"box": {
					"id": "obj-14",
					"maxclass": "comment",
					"numinlets": 1,
					"numoutlets": 0,
					"patching_rect": [580.0, 98.0, 70.0, 20.0],
					"presentation": 1,
					"presentation_rect": [22.0, 40.0, 210.0, 18.0],
					"text": "Autostart (Start PC + play)"
				}
			},
			{
				"box": {
					"id": "obj-15",
					"maxclass": "newobj",
					"numinlets": 1,
					"numoutlets": 1,
					"outlettype": [""],
					"patching_rect": [560.0, 130.0, 108.0, 22.0],
					"text": "prepend autostart"
				}
			},
			{
				"box": {
					"id": "obj-42",
					"maxclass": "live.toggle",
					"numinlets": 1,
					"numoutlets": 1,
					"outlettype": [""],
					"parameter_enable": 1,
					"patching_rect": [700.0, 100.0, 15.0, 15.0],
					"presentation": 1,
					"presentation_rect": [240.0, 24.0, 15.0, 15.0],
					"saved_attribute_attributes": {
						"valueof": {
							"parameter_enum": ["off", "on"],
							"parameter_initial": [0],
							"parameter_initial_enable": 1,
							"parameter_invisible": 1,
							"parameter_longname": "Thru",
							"parameter_mmax": 1,
							"parameter_shortname": "Thru",
							"parameter_type": 2
						}
					},
					"varname": "Thru"
				}
			},
			{
				"box": {
					"id": "obj-43",
					"maxclass": "comment",
					"numinlets": 1,
					"numoutlets": 0,
					"patching_rect": [720.0, 98.0, 60.0, 20.0],
					"presentation": 1,
					"presentation_rect": [258.0, 22.0, 200.0, 18.0],
					"text": "Thru (MIDI in -> out)"
				}
			},
			{
				"box": {
					"id": "obj-44",
					"maxclass": "newobj",
					"numinlets": 1,
					"numoutlets": 1,
					"outlettype": [""],
					"patching_rect": [700.0, 130.0, 80.0, 22.0],
					"text": "prepend thru"
				}
			},
			{
				"box": {
					"id": "obj-16",
					"maxclass": "live.numbox",
					"numinlets": 1,
					"numoutlets": 2,
					"outlettype": ["", "float"],
					"parameter_enable": 1,
					"patching_rect": [420.0, 190.0, 44.0, 15.0],
					"presentation": 1,
					"presentation_rect": [4.0, 60.0, 40.0, 15.0],
					"saved_attribute_attributes": {
						"valueof": {
							"parameter_initial": [1],
							"parameter_initial_enable": 1,
							"parameter_invisible": 1,
							"parameter_longname": "StartPC",
							"parameter_mmax": 127.0,
							"parameter_mmin": 0.0,
							"parameter_shortname": "Start PC",
							"parameter_type": 1,
							"parameter_unitstyle": 0
						}
					},
					"varname": "StartPC"
				}
			},
			{
				"box": {
					"id": "obj-17",
					"maxclass": "comment",
					"numinlets": 1,
					"numoutlets": 0,
					"patching_rect": [470.0, 188.0, 80.0, 20.0],
					"presentation": 1,
					"presentation_rect": [48.0, 58.0, 184.0, 18.0],
					"text": "Start PC (boot pattern)"
				}
			},
			{
				"box": {
					"id": "obj-18",
					"maxclass": "newobj",
					"numinlets": 1,
					"numoutlets": 1,
					"outlettype": [""],
					"patching_rect": [420.0, 220.0, 112.0, 22.0],
					"text": "prepend startpc"
				}
			},
			{
				"box": {
					"id": "obj-19",
					"maxclass": "live.numbox",
					"numinlets": 1,
					"numoutlets": 2,
					"outlettype": ["", "float"],
					"parameter_enable": 1,
					"patching_rect": [560.0, 190.0, 44.0, 15.0],
					"presentation": 1,
					"presentation_rect": [4.0, 78.0, 40.0, 15.0],
					"saved_attribute_attributes": {
						"valueof": {
							"parameter_initial": [0],
							"parameter_initial_enable": 1,
							"parameter_invisible": 1,
							"parameter_longname": "Offset",
							"parameter_mmax": 127.0,
							"parameter_mmin": -127.0,
							"parameter_shortname": "Offset",
							"parameter_type": 1,
							"parameter_unitstyle": 0
						}
					},
					"varname": "Offset"
				}
			},
			{
				"box": {
					"id": "obj-20",
					"maxclass": "comment",
					"numinlets": 1,
					"numoutlets": 0,
					"patching_rect": [610.0, 188.0, 60.0, 20.0],
					"presentation": 1,
					"presentation_rect": [48.0, 76.0, 184.0, 18.0],
					"text": "Offset (scene = PC + offset)"
				}
			},
			{
				"box": {
					"id": "obj-21",
					"maxclass": "newobj",
					"numinlets": 1,
					"numoutlets": 1,
					"outlettype": [""],
					"patching_rect": [560.0, 220.0, 88.0, 22.0],
					"text": "prepend offset"
				}
			},
			{
				"box": {
					"id": "obj-47",
					"maxclass": "live.toggle",
					"numinlets": 1,
					"numoutlets": 1,
					"outlettype": [""],
					"parameter_enable": 1,
					"patching_rect": [700.0, 190.0, 15.0, 15.0],
					"presentation": 1,
					"presentation_rect": [240.0, 42.0, 15.0, 15.0],
					"saved_attribute_attributes": {
						"valueof": {
							"parameter_enum": ["off", "on"],
							"parameter_initial": [0],
							"parameter_initial_enable": 1,
							"parameter_invisible": 1,
							"parameter_longname": "AltCC",
							"parameter_mmax": 1,
							"parameter_shortname": "Alt CC",
							"parameter_type": 2
						}
					},
					"varname": "AltCC"
				}
			},
			{
				"box": {
					"id": "obj-48",
					"maxclass": "comment",
					"numinlets": 1,
					"numoutlets": 0,
					"patching_rect": [720.0, 188.0, 60.0, 20.0],
					"presentation": 1,
					"presentation_rect": [258.0, 40.0, 200.0, 18.0],
					"text": "Alt CC as program"
				}
			},
			{
				"box": {
					"id": "obj-49",
					"maxclass": "newobj",
					"numinlets": 1,
					"numoutlets": 1,
					"outlettype": [""],
					"patching_rect": [700.0, 220.0, 84.0, 22.0],
					"text": "prepend altcc"
				}
			},
			{
				"box": {
					"id": "obj-22",
					"maxclass": "live.numbox",
					"numinlets": 1,
					"numoutlets": 2,
					"outlettype": ["", "float"],
					"parameter_enable": 1,
					"patching_rect": [840.0, 190.0, 44.0, 15.0],
					"presentation": 1,
					"presentation_rect": [240.0, 60.0, 40.0, 15.0],
					"saved_attribute_attributes": {
						"valueof": {
							"parameter_initial": [118],
							"parameter_initial_enable": 1,
							"parameter_invisible": 1,
							"parameter_longname": "AltCCNum",
							"parameter_mmax": 118.0,
							"parameter_mmin": 1.0,
							"parameter_shortname": "Alt CC #",
							"parameter_type": 1,
							"parameter_unitstyle": 0
						}
					},
					"varname": "AltCCNum"
				}
			},
			{
				"box": {
					"id": "obj-23",
					"maxclass": "comment",
					"numinlets": 1,
					"numoutlets": 0,
					"patching_rect": [890.0, 188.0, 60.0, 20.0],
					"presentation": 1,
					"presentation_rect": [284.0, 58.0, 180.0, 18.0],
					"text": "Alt CC number (118)"
				}
			},
			{
				"box": {
					"id": "obj-24",
					"maxclass": "newobj",
					"numinlets": 1,
					"numoutlets": 1,
					"outlettype": [""],
					"patching_rect": [840.0, 220.0, 104.0, 22.0],
					"text": "prepend altccnum"
				}
			},
			{
				"box": {
					"id": "obj-45",
					"maxclass": "live.text",
					"mode": 0,
					"numinlets": 1,
					"numoutlets": 2,
					"outlettype": ["", ""],
					"parameter_enable": 1,
					"patching_rect": [420.0, 260.0, 56.0, 16.0],
					"presentation": 1,
					"presentation_rect": [240.0, 78.0, 56.0, 16.0],
					"saved_attribute_attributes": {
						"valueof": {
							"parameter_enum": ["val1", "val2"],
							"parameter_invisible": 2,
							"parameter_longname": "Resync",
							"parameter_mmax": 1,
							"parameter_shortname": "Resync",
							"parameter_type": 2
						}
					},
					"text": "Resync",
					"texton": "Resync",
					"varname": "Resync"
				}
			},
			{
				"box": {
					"id": "obj-50",
					"maxclass": "comment",
					"numinlets": 1,
					"numoutlets": 0,
					"patching_rect": [480.0, 258.0, 200.0, 20.0],
					"presentation": 1,
					"presentation_rect": [300.0, 76.0, 180.0, 18.0],
					"text": "ask FlowLocal for the pattern"
				}
			},
			{
				"box": {
					"id": "obj-46",
					"maxclass": "newobj",
					"numinlets": 1,
					"numoutlets": 1,
					"outlettype": [""],
					"patching_rect": [420.0, 290.0, 92.0, 22.0],
					"text": "prepend resync"
				}
			},
			{
				"box": {
					"id": "obj-25",
					"maxclass": "comment",
					"numinlets": 1,
					"numoutlets": 0,
					"patching_rect": [140.0, 480.0, 320.0, 20.0],
					"presentation": 1,
					"presentation_rect": [4.0, 102.0, 470.0, 18.0],
					"text": "no program yet",
					"varname": "status_last"
				}
			},
			{
				"box": {
					"id": "obj-26",
					"maxclass": "comment",
					"fontsize": 10.0,
					"numinlets": 1,
					"numoutlets": 0,
					"patching_rect": [480.0, 480.0, 300.0, 18.0],
					"presentation": 1,
					"presentation_rect": [4.0, 122.0, 470.0, 18.0],
					"text": "waiting for Live",
					"varname": "status_info"
				}
			}
		],
		"lines": [
			{ "patchline": { "source": ["obj-2", 0], "destination": ["obj-35", 0] } },
			{ "patchline": { "source": ["obj-35", 0], "destination": ["obj-4", 0] } },
			{ "patchline": { "source": ["obj-2", 0], "destination": ["obj-41", 1] } },
			{ "patchline": { "source": ["obj-41", 0], "destination": ["obj-3", 0] } },
			{ "patchline": { "source": ["obj-4", 2], "destination": ["obj-3", 0] } },
			{ "patchline": { "source": ["obj-4", 3], "destination": ["obj-41", 0] } },
			{ "patchline": { "source": ["obj-5", 0], "destination": ["obj-7", 0] } },
			{ "patchline": { "source": ["obj-7", 2], "destination": ["obj-8", 0] } },
			{ "patchline": { "source": ["obj-7", 1], "destination": ["obj-9", 0] } },
			{ "patchline": { "source": ["obj-7", 0], "destination": ["obj-6", 0] } },
			{ "patchline": { "source": ["obj-8", 0], "destination": ["obj-26", 0] } },
			{ "patchline": { "source": ["obj-9", 0], "destination": ["obj-25", 0] } },
			{ "patchline": { "source": ["obj-6", 0], "destination": ["obj-4", 0] } },
			{ "patchline": { "source": ["obj-4", 0], "destination": ["obj-26", 0] } },
			{ "patchline": { "source": ["obj-4", 1], "destination": ["obj-25", 0] } },
			{ "patchline": { "source": ["obj-10", 0], "destination": ["obj-12", 0] } },
			{ "patchline": { "source": ["obj-12", 0], "destination": ["obj-4", 0] } },
			{ "patchline": { "source": ["obj-13", 0], "destination": ["obj-15", 0] } },
			{ "patchline": { "source": ["obj-15", 0], "destination": ["obj-4", 0] } },
			{ "patchline": { "source": ["obj-42", 0], "destination": ["obj-44", 0] } },
			{ "patchline": { "source": ["obj-44", 0], "destination": ["obj-4", 0] } },
			{ "patchline": { "source": ["obj-16", 0], "destination": ["obj-18", 0] } },
			{ "patchline": { "source": ["obj-18", 0], "destination": ["obj-4", 0] } },
			{ "patchline": { "source": ["obj-19", 0], "destination": ["obj-21", 0] } },
			{ "patchline": { "source": ["obj-21", 0], "destination": ["obj-4", 0] } },
			{ "patchline": { "source": ["obj-47", 0], "destination": ["obj-49", 0] } },
			{ "patchline": { "source": ["obj-49", 0], "destination": ["obj-4", 0] } },
			{ "patchline": { "source": ["obj-22", 0], "destination": ["obj-24", 0] } },
			{ "patchline": { "source": ["obj-24", 0], "destination": ["obj-4", 0] } },
			{ "patchline": { "source": ["obj-45", 0], "destination": ["obj-46", 0] } },
			{ "patchline": { "source": ["obj-46", 0], "destination": ["obj-4", 0] } },
			{ "patchline": { "source": ["obj-30", 0], "destination": ["obj-4", 0] } },
			{ "patchline": { "source": ["obj-31", 0], "destination": ["obj-4", 0] } },
			{ "patchline": { "source": ["obj-32", 0], "destination": ["obj-4", 0] } },
			{ "patchline": { "source": ["obj-33", 0], "destination": ["obj-4", 0] } }
		],
		"dependency_cache": [],
		"autosave": 0
	}
}
