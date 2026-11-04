# Definitional proc to organize widgets for parameters.
proc init_gui { IPINST } {
  ipgui::add_param $IPINST -name "Component_Name"

}

proc update_PARAM_VALUE.DDR_BASE_ADDR { PARAM_VALUE.DDR_BASE_ADDR } {
	# Procedure called to update DDR_BASE_ADDR when any of the dependent parameters in the arguments change
}

proc validate_PARAM_VALUE.DDR_BASE_ADDR { PARAM_VALUE.DDR_BASE_ADDR } {
	# Procedure called to validate DDR_BASE_ADDR
	return true
}

proc update_PARAM_VALUE.IMAGE_HEIGHT { PARAM_VALUE.IMAGE_HEIGHT } {
	# Procedure called to update IMAGE_HEIGHT when any of the dependent parameters in the arguments change
}

proc validate_PARAM_VALUE.IMAGE_HEIGHT { PARAM_VALUE.IMAGE_HEIGHT } {
	# Procedure called to validate IMAGE_HEIGHT
	return true
}

proc update_PARAM_VALUE.IMAGE_WIDTH { PARAM_VALUE.IMAGE_WIDTH } {
	# Procedure called to update IMAGE_WIDTH when any of the dependent parameters in the arguments change
}

proc validate_PARAM_VALUE.IMAGE_WIDTH { PARAM_VALUE.IMAGE_WIDTH } {
	# Procedure called to validate IMAGE_WIDTH
	return true
}


proc update_MODELPARAM_VALUE.IMAGE_WIDTH { MODELPARAM_VALUE.IMAGE_WIDTH PARAM_VALUE.IMAGE_WIDTH } {
	# Procedure called to set VHDL generic/Verilog parameter value(s) based on TCL parameter value
	set_property value [get_property value ${PARAM_VALUE.IMAGE_WIDTH}] ${MODELPARAM_VALUE.IMAGE_WIDTH}
}

proc update_MODELPARAM_VALUE.IMAGE_HEIGHT { MODELPARAM_VALUE.IMAGE_HEIGHT PARAM_VALUE.IMAGE_HEIGHT } {
	# Procedure called to set VHDL generic/Verilog parameter value(s) based on TCL parameter value
	set_property value [get_property value ${PARAM_VALUE.IMAGE_HEIGHT}] ${MODELPARAM_VALUE.IMAGE_HEIGHT}
}

proc update_MODELPARAM_VALUE.DDR_BASE_ADDR { MODELPARAM_VALUE.DDR_BASE_ADDR PARAM_VALUE.DDR_BASE_ADDR } {
	# Procedure called to set VHDL generic/Verilog parameter value(s) based on TCL parameter value
	set_property value [get_property value ${PARAM_VALUE.DDR_BASE_ADDR}] ${MODELPARAM_VALUE.DDR_BASE_ADDR}
}

