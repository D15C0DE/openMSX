# convenience wrappers around the low level savestate commands

namespace eval savestate {

proc savestate_common { name } {
	uplevel {
		if {$name == ""} { set name "quicksave" }
		set directory [file normalize $::env(OPENMSX_USER_DATA)/../savestates]
		set fullname [file join $directory ${name}.xml.gz]
		set png [file join $directory ${name}.png]
	}
}

proc savestate { {name ""} } {
	savestate_common $name
	if {[catch { screenshot -raw -doublesize $png }]} {
		# some renderers don't support msx-only screenshots
		if {[catch { screenshot $png }]} {
			# even this failed, but (try to) remove old screenhot
			# to avoid confusion
			catch { file delete $png }
		}
	}
	set currentID [machine]
	file mkdir $directory
	store_machine $currentID $fullname
	return $name
}

proc loadstate { {name ""} } {
	savestate_common $name
	# work around namespace probelm with the restore_machine command:
	set newID [namespace eval :: [list restore_machine $fullname]]
	set currentID [machine]
	if {$currentID != ""} { delete_machine $currentID }
	activate_machine $newID
	return $name
}

proc list_savestates {} {
	set directory [file normalize $::env(OPENMSX_USER_DATA)/../savestates]
	set result [list]
	foreach f [glob -tails -directory $directory -nocomplain *.xml.gz] {
		lappend result [file rootname [file rootname $f]]
	}
	return [lsort $result]
}

proc delete_savestate { {name ""} } {
	savestate_common $name
	catch { file delete -- $fullname }
	catch { file delete -- $png }
	return ""
}

proc savestate_tab { args } {
	return [list_savestates]
}

# savestate
set_help_text savestate \
{savestate [<name>]

Create a snapshot of the current emulated MSX machine.

Optionally you can specify a name for the savestate. If you omit this the default name 'quicksave' will be taken.

See also 'loadstate', 'list_savestates', 'delete_savestate'.
}
set_tabcompletion_proc savestate [namespace code savestate_tab]

# loadstate
set_help_text loadstate \
{loadstate [<name>]

Restore a previously created savestate.

You can specify the name of the savestate that should be loaded. If you omit this name, the default savestate will be loaded.

See also 'savestate', 'list_savestates', 'delete_savestate'.
}
set_tabcompletion_proc loadstate [namespace code savestate_tab]

# list_savestates
set_help_text list_savestates \
{list_savestates

Return the names of all previously created savestates.

See also 'savestate', 'loadstate', 'delete_savestate'.
}

# delete_savestate
set_help_text delete_savestate \
{delete_savestate [<name>]

Delete a previously created savestate.

See also 'savestate', 'loadstate', 'list_savestates'.
}
set_tabcompletion_proc delete_savestate [namespace code savestate_tab]


# keybindings
if {$tcl_platform(os) == "Darwin"} {
	bind_default META+S savestate
	bind_default META+R loadstate
} else {
	bind_default ALT+F8 savestate
	bind_default ALT+F7 loadstate
}

namespace export savestate
namespace export loadstate
namespace export delete_savestate
namespace export list_savestates

} ;# namespace savestate

namespace import savestate::*
