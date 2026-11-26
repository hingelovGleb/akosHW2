import subprocess as sp
import time
import sys
from pathlib import Path

root = Path(__file__).resolve().parents[1]
exe = str(root / 'bin/battleship')
port = sys.argv[1] if len(sys.argv) > 1 else '19084'

def start(role, *args):
    f = open(root / 'tests' / (role + str(len(processes)) + '.log'), 'w')
    p = sp.Popen([exe, role, '127.0.0.1', port, *args], stdout=f, stderr=f)
    f.close()
    processes.append(p)
    return p

processes = []
try:
    server = start('server', '--seed=5', '--delay=30', '--log=none')
    time.sleep(.2)
    a = start('observer')
    b = start('observer')
    time.sleep(.2)
    start('player', '0')
    start('player', '1')
    time.sleep(.7)
    a.terminate()
    a.wait(timeout=3)
    time.sleep(.2)
    c = start('observer')
    assert server.wait(timeout=20) == 0
    for p in processes[2:]:
        assert p.wait(timeout=3) == 0
    for name in ['server0', 'observer2', 'observer5']:
        log = (root / 'tests' / (name + '.log')).read_text()
        assert 'Победил игрок 0 за 110 ходов' in log, name
    assert 'Победил' not in (root / 'tests/observer1.log').read_text()
    print('OK: два наблюдателя, отключение и повторное подключение, результат совпал')
finally:
    for p in processes:
        if p.poll() is None:
            p.terminate()
    for p in processes:
        p.wait(timeout=3)
