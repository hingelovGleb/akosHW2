import subprocess as sp
import time
import sys
from pathlib import Path

root = Path(__file__).resolve().parents[1]
exe = str(root / 'bin/battleship')
port = sys.argv[1] if len(sys.argv) > 1 else '19085'
processes = []

def start(role, *args):
    f = open(root / 'tests' / ('reconnect_' + role + str(len(processes)) + '.log'), 'w')
    p = sp.Popen([exe, role, '127.0.0.1', port, *args], stdout=f, stderr=f)
    f.close()
    processes.append(p)
    return p

log = root / 'tests/reconnect_server0.log'

def wait_for(text):
    for _ in range(100):
        if text in log.read_text():
            return
        time.sleep(.05)
    raise AssertionError(text)

try:
    server = start('server', '--seed=5', '--delay=40', '--log=none')
    time.sleep(.2)
    observer = start('observer')
    a = start('player', '0')
    b = start('player', '1')
    wait_for('Ход  10.')
    a.kill()
    a.wait(timeout=3)
    wait_for('Игрок 0 отключился')
    time.sleep(.2)
    before = log.read_text()
    time.sleep(.2)
    assert server.poll() is None and log.read_text() == before
    a = start('player', '0')
    wait_for('Ход  25.')
    b.terminate()
    b.wait(timeout=3)
    wait_for('Игрок 1 отключился')
    b = start('player', '1')
    assert server.wait(timeout=20) == 0
    for p in (a, b, observer):
        assert p.wait(timeout=3) == 0
    text = log.read_text()
    assert text.count('Игрок 0 подключился') == 2
    assert text.count('Игрок 1 подключился') == 2
    for line in ('Победил игрок 0 за 110 ходов',
                 'Игрок 0: выстрелов 60, попаданий 20, уничтожено 10',
                 'Игрок 1: выстрелов 50, попаданий 10, уничтожено 2'):
        assert line in text
    # Сравниваем не только итог, но и каждый выстрел с обычной партией.
    baseline = (root / 'tests/server0.log').read_text()
    def shots(s):
        return [line for line in s.splitlines() if ' -> ' in line and line.startswith('Ход ')]
    assert shots(text) == shots(baseline) and len(shots(text)) == 110
    print('OK: оба игрока переподключены, все 110 ходов совпали')
finally:
    for p in processes:
        if p.poll() is None:
            p.terminate()
    for p in processes:
        p.wait(timeout=3)
