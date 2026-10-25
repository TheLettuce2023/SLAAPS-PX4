import math
import struct


class CodecError(ValueError):
    '''Invalid C0a payload or field dictionary.'''


PROTOCOL_VERSION = 0x00010000
_REQUEST = struct.Struct('<QQQQQQQQfffI6B4s')
_SESSION = struct.Struct('<QQQQQQQQI4B4s')
_STATUS = struct.Struct('<QQQQQQQQQQQIHHH8B')
_U8_MAX = (1 << 8) - 1
_U16_MAX = (1 << 16) - 1
_U32_MAX = (1 << 32) - 1
_U64_MAX = (1 << 64) - 1

_REQUEST_FIELDS = (
    'source_id', 'session_id', 'sequence', 'producer_monotonic_us',
    'producer_expiry_monotonic_us', 'controller_instance_id', 'transition_epoch',
    'clock_mapping_epoch', 'acceleration_x_frd_m_s2', 'acceleration_y_frd_m_s2',
    'acceleration_z_frd_m_s2', 'protocol_version', 'target_system',
    'target_component', 'frame', 'quantity', 'reference_point', 'flags', 'reserved',
)
_SESSION_FIELDS = (
    'controller_instance_id', 'transition_epoch', 'clock_mapping_epoch', 'source_id',
    'session_id', 'operation_sequence', 'producer_acknowledgement_monotonic_us',
    'acknowledged_status_sequence', 'protocol_version', 'target_system',
    'target_component', 'operation', 'flags', 'reserved',
)
_STATUS_FIELDS = (
    'controller_instance_id', 'transition_epoch', 'clock_mapping_epoch', 'source_id',
    'session_id', 'correlation_sequence', 'last_accepted_sequence', 'validator_time_us',
    'status_sequence', 'rejection_count', 'conservative_age_us', 'protocol_version',
    'decision_code', 'first_latched_code', 'status_flags', 'target_system',
    'target_component', 'status_class', 'arm_state', 'research_mode_state',
    'session_state', 'transport_state', 'clock_mapping_state',
)


def _fields(fields, names):
    if type(fields) is not dict or set(fields) != set(names):
        raise CodecError('field keys must match exactly')
    return {name: fields[name] for name in names}


def _uint(value, maximum, name, nonzero=False):
    if type(value) is not int or value < 0 or value > maximum or (nonzero and value == 0):
        raise CodecError('%s is not a valid unsigned integer' % name)
    return value


def _protocol(value):
    if _uint(value, _U32_MAX, 'protocol_version') != PROTOCOL_VERSION:
        raise CodecError('unsupported protocol_version')
    return value


def _float32(value, name):
    if type(value) not in (int, float):
        raise CodecError('%s must be an int or float' % name)
    try:
        value = float(value)
        packed = struct.pack('<f', value)
    except (OverflowError, ValueError, struct.error):
        raise CodecError('%s is not binary32 representable' % name) from None
    value = struct.unpack('<f', packed)[0]
    if not math.isfinite(value):
        raise CodecError('%s must be finite' % name)
    return value


def _reserved(value):
    if type(value) is not bytes or value != b'\0\0\0\0':
        raise CodecError('reserved must be four zero bytes')
    return value


def _payload(payload, minimum, codec, name):
    if type(payload) is not bytes or not minimum <= len(payload) <= codec.size:
        raise CodecError('%s payload has an invalid length' % name)
    return payload + bytes(codec.size - len(payload))


def _trim(payload, minimum, trim):
    if type(trim) is not bool:
        raise CodecError('trim must be a bool')
    if not trim:
        return payload
    return payload[:max(minimum, len(payload.rstrip(b'\0')))]


def _request_values(fields):
    values = _fields(fields, _REQUEST_FIELDS)
    for name in ('source_id', 'session_id', 'controller_instance_id', 'transition_epoch', 'clock_mapping_epoch'):
        values[name] = _uint(values[name], _U64_MAX, name, nonzero=True)
    for name in ('sequence', 'producer_monotonic_us', 'producer_expiry_monotonic_us'):
        values[name] = _uint(values[name], _U64_MAX, name)
    if values['producer_expiry_monotonic_us'] <= values['producer_monotonic_us']:
        raise CodecError('producer expiry must be strictly after creation')
    for name in ('acceleration_x_frd_m_s2', 'acceleration_y_frd_m_s2', 'acceleration_z_frd_m_s2'):
        values[name] = _float32(values[name], name)
    values['protocol_version'] = _protocol(values['protocol_version'])
    for name in ('target_system', 'target_component'):
        values[name] = _uint(values[name], _U8_MAX, name, nonzero=True)
    for name in ('frame', 'quantity', 'reference_point'):
        values[name] = _uint(values[name], _U8_MAX, name)
        if values[name] != 1:
            raise CodecError('%s must be the v1 value 1' % name)
    values['flags'] = _uint(values['flags'], _U8_MAX, 'flags')
    if values['flags'] != 0:
        raise CodecError('request flags must be zero')
    values['reserved'] = _reserved(values['reserved'])
    return values


def _session_values(fields):
    values = _fields(fields, _SESSION_FIELDS)
    for name in ('controller_instance_id', 'transition_epoch', 'clock_mapping_epoch', 'source_id', 'session_id'):
        values[name] = _uint(values[name], _U64_MAX, name, nonzero=True)
    for name in ('operation_sequence', 'producer_acknowledgement_monotonic_us', 'acknowledged_status_sequence'):
        values[name] = _uint(values[name], _U64_MAX, name)
    values['protocol_version'] = _protocol(values['protocol_version'])
    for name in ('target_system', 'target_component'):
        values[name] = _uint(values[name], _U8_MAX, name, nonzero=True)
    values['operation'] = _uint(values['operation'], _U8_MAX, 'operation')
    if values['operation'] not in (1, 2, 3):
        raise CodecError('session operation is invalid')
    values['flags'] = _uint(values['flags'], _U8_MAX, 'flags')
    if values['flags'] != 0:
        raise CodecError('session flags must be zero')
    values['reserved'] = _reserved(values['reserved'])
    return values


def encode_request(fields, *, trim=True):
    '''Encode a request payload without making an admission decision.'''
    values = _request_values(fields)
    return _trim(_REQUEST.pack(*(values[name] for name in _REQUEST_FIELDS)), 85, trim)


def decode_request(payload):
    '''Decode a request payload without making an admission decision.'''
    values = _REQUEST.unpack(_payload(payload, 85, _REQUEST, 'request'))
    return _request_values(dict(zip(_REQUEST_FIELDS, values)))


def encode_session(fields, *, trim=True):
    '''Encode an administrative session payload.'''
    values = _session_values(fields)
    return _trim(_SESSION.pack(*(values[name] for name in _SESSION_FIELDS)), 71, trim)


def decode_session(payload):
    '''Decode an administrative session payload.'''
    values = _SESSION.unpack(_payload(payload, 71, _SESSION, 'session'))
    return _session_values(dict(zip(_SESSION_FIELDS, values)))


_ELIGIBLE = 1 << 0
_SYNTACTICALLY_VALID = 1 << 1
_LATCHED = 1 << 2
_AGE_VALID = 1 << 3
_LAST_SEQUENCE = 1 << 4
_AUTHENTICATED = 1 << 5
_STATUS_FLAGS = _ELIGIBLE | _SYNTACTICALLY_VALID | _LATCHED | _AGE_VALID | _LAST_SEQUENCE | _AUTHENTICATED


def _enum(value, name, allowed):
    value = _uint(value, _U8_MAX, name)
    if value not in allowed:
        raise CodecError('%s is invalid' % name)
    return value


def _status_values(fields):
    values = _fields(fields, _STATUS_FIELDS)
    values['controller_instance_id'] = _uint(values['controller_instance_id'], _U64_MAX,
                                             'controller_instance_id', nonzero=True)
    values['transition_epoch'] = _uint(values['transition_epoch'], _U64_MAX,
                                       'transition_epoch', nonzero=True)
    for name in ('clock_mapping_epoch', 'source_id', 'session_id', 'correlation_sequence',
                 'last_accepted_sequence', 'validator_time_us', 'status_sequence',
                 'rejection_count', 'conservative_age_us'):
        values[name] = _uint(values[name], _U64_MAX, name)
    values['protocol_version'] = _protocol(values['protocol_version'])
    values['decision_code'] = _uint(values['decision_code'], _U16_MAX, 'decision_code')
    if not (0 <= values['decision_code'] <= 38 or 64 <= values['decision_code'] <= 83):
        raise CodecError('decision_code is unassigned in v1')
    values['first_latched_code'] = _uint(values['first_latched_code'], _U16_MAX, 'first_latched_code')
    values['status_flags'] = _uint(values['status_flags'], _U16_MAX, 'status_flags')
    if values['status_flags'] & ~_STATUS_FLAGS:
        raise CodecError('undefined status flag bit')
    values['target_system'] = _uint(values['target_system'], _U8_MAX, 'target_system')
    values['target_component'] = _uint(values['target_component'], _U8_MAX, 'target_component')
    values['status_class'] = _enum(values['status_class'], 'status_class', (1, 2, 3, 4, 5))
    values['arm_state'] = _enum(values['arm_state'], 'arm_state', (1, 2, 3))
    values['research_mode_state'] = _enum(values['research_mode_state'], 'research_mode_state', (1, 2, 3))
    values['session_state'] = _enum(values['session_state'], 'session_state', (1, 2, 3, 4, 5))
    values['transport_state'] = _enum(values['transport_state'], 'transport_state', (1, 2, 3))
    values['clock_mapping_state'] = _enum(values['clock_mapping_state'], 'clock_mapping_state', (1, 2, 3))

    targets_zero = values['target_system'] == 0 and values['target_component'] == 0
    targets_nonzero = values['target_system'] != 0 and values['target_component'] != 0
    if not targets_nonzero and not (targets_zero and values['status_class'] == 5):
        raise CodecError('status target IDs are inconsistent')
    if values['status_class'] in (3, 4, 5) and values['correlation_sequence'] != 0:
        raise CodecError('unsolicited status must have zero correlation_sequence')
    if values['clock_mapping_state'] == 1 and values['clock_mapping_epoch'] == 0:
        raise CodecError('valid clock map requires a clock_mapping_epoch')

    flags = values['status_flags']
    if not flags & _AGE_VALID and values['conservative_age_us'] != 0:
        raise CodecError('absent age flag requires zero conservative_age_us')
    if not flags & _LAST_SEQUENCE and values['last_accepted_sequence'] != 0:
        raise CodecError('absent last sequence flag requires zero last_accepted_sequence')

    latched = bool(flags & _LATCHED)
    if latched != (values['session_state'] == 5):
        raise CodecError('latch flag and session_state must agree')
    if latched and values['first_latched_code'] not in (18, 19):
        raise CodecError('latched status requires first clock-regression cause')
    if not latched and values['first_latched_code'] != 0:
        raise CodecError('unlatched status requires zero first_latched_code')
    if values['decision_code'] in (18, 19, 37) and not latched:
        raise CodecError('latched decision requires latched state')

    eligible = bool(flags & _ELIGIBLE)
    if values['decision_code'] == 0 and (values['status_class'] != 5 or eligible):
        raise CodecError('decision zero is only a non-eligible snapshot')
    if eligible and not (
            values['decision_code'] == 1 and values['status_class'] in (1, 5)
            and flags & _SYNTACTICALLY_VALID and flags & _AUTHENTICATED
            and values['arm_state'] == 2 and values['research_mode_state'] == 2
            and values['session_state'] == 2 and values['transport_state'] == 1
            and values['clock_mapping_state'] == 1 and values['source_id'] != 0
            and values['session_id'] != 0 and not latched):
        raise CodecError('eligible status is internally inconsistent')
    if values['decision_code'] == 2 and (values['arm_state'] != 1 or eligible):
        raise CodecError('validated-disarmed status is internally inconsistent')
    return values


def encode_status(fields, *, trim=True):
    '''Encode a structurally consistent status payload.'''
    values = _status_values(fields)
    return _trim(_STATUS.pack(*(values[name] for name in _STATUS_FIELDS)), 106, trim)


def decode_status(payload):
    '''Decode a structurally consistent status payload.'''
    values = _STATUS.unpack(_payload(payload, 106, _STATUS, 'status'))
    return _status_values(dict(zip(_STATUS_FIELDS, values)))
