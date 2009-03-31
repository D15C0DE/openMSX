# VU-meters, just for fun.
#
# Search in the script for 'customized' to see what you could customize ;-)
#
# TODO:
# - optimize  more?
# - implement volume for MoonSound FM (tricky stuff)
# - actually, don't use regs to calc volume but actual wave data (needs openMSX
#   changes)
# - handle insertion/removal of sound devices (needs openMSX changes)
# - see also in get_volume_expr_for_channel
#
# Thanks to BiFi for helping out with the expressions for the devices.
# Thanks to Wouter for the Tcl support.

set_help_text toggle_vu_meters \
{Puts a rough volume unit display for each channel and for all sound chips on
the On-Screen-Display. Use the command again to remove it. Note: it cannot
handle run-time insertion/removal of sound devices. It can handle changing
of machines, though. MoonSound FM is not supported yet.
Note that displaying these VU-meters may cause quite some CPU load!}

namespace eval vu_meters {

variable vu_meters_active false
variable volume_cache
variable volume_expr
variable nof_channels
variable bar_length
variable soundchips
variable vu_meter_trigger_id

proc vu_meters_init {} {

	variable volume_cache
	variable volume_expr
	variable nof_channels
	variable bar_length
	variable soundchips
	variable vu_meter_trigger_id

	# create root object for vu_meters
	osd create rectangle vu_meters \
		-scaled true \
		-alpha 0 \
		-z 1

	set soundchips [list]

	# skip devices with only one channel (they are not very interesting)
	foreach soundchip [machine_info sounddevice] {
		# determine number of channels
		set channel_count 1
		while {[info exists ::${soundchip}_ch${channel_count}_mute]} {
			set channel [expr $channel_count - 1]
			incr channel_count
			# while we're at it, also create the volume cache and the expressions
			set volume_cache($soundchip,$channel) -1
			set volume_expr($soundchip,$channel) [get_volume_expr_for_channel $soundchip $channel]
		}
		incr channel_count -1
		if {$channel_count > 1} {
			lappend soundchips $soundchip
			set nof_channels($soundchip) $channel_count
		}
	}
	
	set bar_width 2; # this value could be customized
	set vu_meter_title_height 8; # this value could be customized
	set bar_length [expr (320 - [llength $soundchips]) / [llength $soundchips] ]

	# create widgets for each sound chip:

	set vu_meter_offset 0

	foreach soundchip $soundchips {

		# create surrounding widget for this chip
		osd create rectangle vu_meters.$soundchip \
			-rgba 0x00000080 \
			-x ${vu_meter_offset} \
			-y 0 \
			-w $bar_length \
			-h [expr $vu_meter_title_height + 1 + $nof_channels($soundchip) * ($bar_width + 1)] \
			-clip true
		osd create text vu_meters.${soundchip}.title \
			-x 1 \
			-y 1 \
			-rgba 0xffffffff \
			-text $soundchip \
			-size [expr $vu_meter_title_height - 1]

		# create vu meters for this sound chip
		for {set channel 0} {$channel < $nof_channels($soundchip)}  {incr channel} {
				osd create rectangle vu_meters.${soundchip}.ch${channel} \
				-rgba 0xff0000ff \
				-x 0 \
				-y [expr $vu_meter_title_height + 1 + ( ($bar_width + 1) * $channel) ] \
				-w 0 \
				-h $bar_width \
		}

		incr vu_meter_offset [expr $bar_length + 1]
	}

	set vu_meter_trigger_id [after machine_switch [namespace code vu_meters_reset]]
}

proc update_meters {} {
	variable vu_meters_active
	variable volume_cache
	variable volume_expr
	variable nof_channels
	variable soundchips

	# update meters with the volumes
	if {!$vu_meters_active} return

	foreach soundchip $soundchips {
		for {set channel 0} {$channel < $nof_channels($soundchip)}  {incr channel} {
			set new_volume [eval $volume_expr($soundchip,$channel)]
			if {$volume_cache($soundchip,$channel) != $new_volume} {
				update_meter "vu_meters.${soundchip}.ch${channel}" $new_volume
				set volume_cache($soundchip,$channel) $new_volume
			}
		}
	}
	# here you can customize the update frequency (to reduce CPU load)
	#after time 0.05 [namespace code update_meters]
	after frame [namespace code update_meters]
}

proc update_meter {meter volume} {
	variable bar_length

	set byte_val [expr round(255 * $volume)]
	osd configure $meter \
		-w [expr $bar_length * $volume] \
		-rgba [expr ($byte_val << 24) + ((255 ^ $byte_val) << 8) + ( 0x008000C0)]
}

proc get_volume_expr_for_channel {soundchip channel} {
	# note: channel number starts with 0 here
	switch [machine_info sounddevice $soundchip] {
		"PSG" {
			return "expr ( (\[debug read \"${soundchip} regs\" [expr $channel + 8] \] &0xF) ) / 15.0"
		}
		"MoonSound wave-part" {
			return "expr (127 - (\[debug read \"${soundchip} regs\" [expr $channel + 0x50] \] >> 1) ) / 127.0 * \[expr \[debug read \"${soundchip} regs\" [expr $channel + 0x68] \] >> 7\]";
		}
		"Konami SCC" {
			return "expr ( (\[debug read \"${soundchip} SCC\" [expr $channel + 0xAA] \] &0xF) ) / 15.0"
		}
		"MSX-MUSIC" {
			return "expr (15 - (\[debug read \"${soundchip} regs\" [expr $channel + 0x30] \] &0xF) ) / 15.0 * \[ expr ( (\[debug read \"${soundchip} regs\" [expr $channel + 0x20] ] &16) ) >> 4\]";# carrier total level, but only when key bit is on for this channel
		}
		"MSX-AUDIO" {
			if {$channel == 11} { ;# ADPCM
				return "expr \[debug read \"${soundchip} regs\" 0x12\] / 255.0";# can we output 0 when no sample is playing?
			} else {
				set offset $channel
				if {$channel > 2} {
					incr offset 5
				}
				if {$channel > 5} {
					incr offset 5
				}
				return "expr (63 - ( \[debug read \"${soundchip} regs\" [expr $offset + 0x43] \] &63) ) / 63.0 * \[ expr ( (\[debug read \"${soundchip} regs\" [expr $channel + 0xB0] ] &32) ) >> 5\] ";# carrier total level, but only when key bit is on for this channel
			}
		}
		default {
			return "expr 0"
		}
	}
}

proc vu_meters_reset {} {
	if {!$vu_meters_active} {
		error "Please fix a bug in this script!"
	}
	toggle_vu_meters
	toggle_vu_meters
}

proc toggle_vu_meters {} {
	variable vu_meters_active
	variable vu_meter_trigger_id

	if {$vu_meters_active} {
		catch {after cancel $vu_meter_trigger_id}
		set vu_meters_active false
		osd destroy vu_meters
		unset soundchips bar_length volume_cache
	} else {
		set vu_meters_active true
		vu_meters_init
		update_meters
	}
}

namespace export toggle_vu_meters

} ;# namespace vu_meters

namespace import vu_meters::*
