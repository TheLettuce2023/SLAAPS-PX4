############################################################################
#
#   Copyright (c) 2015 PX4 Development Team. All rights reserved.
#
# Redistribution and use in source and binary forms, with or without
# modification, are permitted provided that the following conditions
# are met:
#
# 1. Redistributions of source code must retain the above copyright
#    notice, this list of conditions and the following disclaimer.
# 2. Redistributions in binary form must reproduce the above copyright
#    notice, this list of conditions and the following disclaimer in
#    the documentation and/or other materials provided with the
#    distribution.
# 3. Neither the name PX4 nor the names of its contributors may be
#    used to endorse or promote products derived from this software
#    without specific prior written permission.
#
# THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
# "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
# LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
# FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE
# COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
# INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING,
# BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS
# OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED
# AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
# LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN
# ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
# POSSIBILITY OF SUCH DAMAGE.
#
############################################################################

if(CONFIG_SLAAPS_ROUTING_TEST)
	# Native capture-only fixture exercises real MixingOutput calibration. Reuse
	# physical driver metadata rather than inventing test parameter definitions.
	set(native_pwm_config "${CMAKE_CURRENT_BINARY_DIR}/slaaps_native_pwm.yaml")
	set(native_pwm_generator "${PX4_SOURCE_DIR}/Tools/slaaps/configuration/generate_native_pwm_metadata.py")
	set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
		"${native_pwm_generator}"
		"${PX4_SOURCE_DIR}/src/drivers/px4io/module.yaml"
		"${PX4_SOURCE_DIR}/src/drivers/pwm_out/module.yaml"
		"${PX4_SOURCE_DIR}/boards/px4/io-v2/src/timer_config.cpp"
		"${PX4_SOURCE_DIR}/boards/px4/fmu-v6x/src/timer_config.cpp")
	execute_process(COMMAND ${PYTHON_EXECUTABLE} "${native_pwm_generator}" "${native_pwm_config}"
		RESULT_VARIABLE native_pwm_result)
	if(NOT native_pwm_result EQUAL 0)
		message(FATAL_ERROR "SLAAPS native PWM metadata generation failed")
	endif()
	set_property(GLOBAL APPEND PROPERTY PX4_MODULE_CONFIG_FILES "${native_pwm_config}")
	set(module_config "")
elseif(${PX4_BOARD_VENDOR}_${PX4_BOARD_MODEL} STREQUAL "px4_sitl")
	set(module_config "module_sim.yaml")
else()
	set(module_config "module_hil.yaml")
endif()

px4_add_module(
	MODULE modules__simulation__pwm_out_sim
	MAIN pwm_out_sim
	SRCS
		PWMSim.cpp
	MODULE_CONFIG
		${module_config}
	DEPENDS
		mixer_module
	)
