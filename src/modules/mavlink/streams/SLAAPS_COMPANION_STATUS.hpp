/****************************************************************************
 *
 *   Copyright (c) 2026 PX4 Development Team. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in
 *    the documentation and/or other materials provided with the
 *    distribution.
 * 3. Neither the name PX4 nor the names of its contributors may be
 *    used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
 * FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE
 * COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING,
 * BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS
 * OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED
 * AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN
 * ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 *
 ****************************************************************************/

#if defined(CONFIG_MODULES_SLAAPS) && defined(MAVLINK_MSG_ID_RESEARCH_ACCELERATION_STATUS)
#ifndef SLAAPS_COMPANION_STATUS_HPP
#define SLAAPS_COMPANION_STATUS_HPP

class MavlinkStreamSlaapsCompanionStatus : public MavlinkStream
{
public:
	static MavlinkStream *new_instance(Mavlink *mavlink) { return new MavlinkStreamSlaapsCompanionStatus(mavlink); }
	static constexpr const char *get_name_static() { return "SLAAPS_COMPANION_STATUS"; }
	static constexpr uint32_t get_id_static() { return MAVLINK_MSG_ID_RESEARCH_ACCELERATION_STATUS; }
	const char *get_name() const override { return get_name_static(); }
	uint32_t get_id() override { return get_id_static(); }
	unsigned get_size() override { return 0; } // Event-driven, bounded queue and actual TX space govern each drain.

private:
	explicit MavlinkStreamSlaapsCompanionStatus(Mavlink *mavlink) : MavlinkStream(mavlink) {}
	bool send() override { return _mavlink->send_slaaps_companion_status(); }
};

#endif // SLAAPS_COMPANION_STATUS_HPP
#endif // CONFIG_MODULES_SLAAPS && MAVLINK_MSG_ID_RESEARCH_ACCELERATION_STATUS
