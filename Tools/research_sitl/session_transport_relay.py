"""Private test relay between distinct namespaces; never grants output authority."""
import json
import threading
from gz.transport13 import Node, NodeOptions
from gz.msgs10.double_v_pb2 import Double_V


class SessionTransportRelay:
    def __init__(self, partition, consumer, plant, log):
        assert consumer != plant
        options = NodeOptions()
        options.partition = partition
        self.node = Node(options)
        self.lock = threading.RLock()
        self.clock = 0
        self.mode = 'pass'
        self.pending = []
        self.closed = False
        self.log = log.open('x', buffering=1)
        self.subscriptions = []
        self.publishers = {}
        self.callbacks = []
        for kind in ('command', 'request', 'ack', 'reply', 'status'):
            incoming, outgoing = (consumer, plant) if kind in ('command', 'request') else (plant, consumer)
            self.publishers[kind] = self.node.advertise(outgoing + '/' + kind, Double_V)
            callback = lambda message, kind=kind: self.receive(kind, message)
            self.callbacks.append(callback)
            topic = incoming + '/' + kind
            self.node.subscribe(Double_V, topic, callback)
            self.subscriptions.append(topic)

    def record(self, action, kind, message=None):
        self.log.write(json.dumps(dict(action=action, kind=kind, sim_time_us=self.clock,
                                      data=list(message.data) if message is not None else [])) + '\n')

    def set_mode(self, mode):
        assert mode in ('pass', 'drop_ack', 'delay_ack', 'drop_command', 'drop_grant', 'drop_disarm')
        with self.lock:
            self.mode = mode
            self.record('mode', mode)

    def receive(self, kind, message):
        with self.lock:
            if self.closed:
                return
            copy = Double_V()
            copy.CopyFrom(message)
            if kind == 'status' and len(copy.data) == 7:
                self.clock = max(self.clock, int(copy.data[3]))
            self.record('receive', kind, copy)
            drop_reply = kind == 'reply' and len(copy.data) >= 2 and (
                (self.mode == 'drop_grant' and copy.data[1] == 6)
                or (self.mode == 'drop_disarm' and copy.data[1] == 7))
            if drop_reply or (kind == 'ack' and self.mode == 'drop_ack') or (kind == 'command' and self.mode == 'drop_command'):
                self.record('drop', kind, copy)
            elif kind == 'ack' and self.mode == 'delay_ack':
                # 80 ms exceeds the unchanged 50 ms completion deadline.
                self.pending.append((max(self.clock, int(copy.data[6])) + 80000, copy))
                self.record('delay', kind, copy)
            else:
                self.publishers[kind].publish(copy)
            remaining = []
            for due, delayed in self.pending:
                if self.clock >= due:
                    self.publishers['ack'].publish(delayed)
                    self.record('release', 'ack', delayed)
                else:
                    remaining.append((due, delayed))
            self.pending = remaining

    def close(self):
        with self.lock:
            self.closed = True
        for topic in self.subscriptions:
            self.node.unsubscribe(topic)
        with self.lock:
            self.log.close()
