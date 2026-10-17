if(NOT RESEARCH_C1C_CAPTURE)
	message(FATAL_ERROR "C1C external messages require explicit capture opt-in")
endif()
set(config_msg_list_external ResearchStateReceipt.msg ResearchStateSummary.msg PARENT_SCOPE)
