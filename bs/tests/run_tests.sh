#!/bin/sh
cd "$(dirname "$0")/.." || exit 1
BIN=./bin/battleship
fail=0
check() { # описание, ожидаемый код, фактический код
    if [ "$2" = "$3" ]; then echo "ok   $1"; else echo "FAIL $1 (ждали $2, получили $3)"; fail=1; fi
}

for seed in 1 2 3 4 5; do
    $BIN --seed=$seed --delay=0 --log=none > /tmp/bs_out.txt; check "бой seed=$seed" 0 $?
    grep -q "Победил" /tmp/bs_out.txt; check "  есть победитель" 0 $?
done

for t in 0 1; do for a in 0 1; do
    $BIN --seed=9 --delay=0 --log=none --touch=$t --again=$a > /dev/null; check "touch=$t again=$a" 0 $?
done; done

$BIN -c configs/classic.cfg --delay=0 --seed=3 --log=none > /dev/null; check "classic.cfg" 0 $?
$BIN -c configs/touch_switch.cfg --seed=3 --log=none > /tmp/bs_out.txt; check "touch_switch.cfg" 0 $?
grep -q "лимит" /tmp/bs_out.txt; check "  сработал лимит ходов" 0 $?

$BIN --seed=7 --delay=0 --log=none > /tmp/bs_1.txt
$BIN --seed=7 --delay=0 --log=none > /tmp/bs_2.txt
cmp -s /tmp/bs_1.txt /tmp/bs_2.txt; check "тот же seed - тот же бой" 0 $?

$BIN --size=3 > /dev/null 2>&1; check "size=3 отклонён" 1 $?
$BIN --foo=1 > /dev/null 2>&1; check "неизвестный параметр" 1 $?
$BIN -c no_such_file.cfg > /dev/null 2>&1; check "нет файла настроек" 2 $?

$BIN --seed=1 --delay=0 --log=/tmp/bs_test.log > /dev/null
[ -s /tmp/bs_test.log ]; check "журнал записался" 0 $?

$BIN --seed=1 --delay=200 --log=none > /dev/null & pid=$!
sleep 1; kill -INT $pid; wait $pid; check "Ctrl+C даёт код 130" 130 $?

$BIN --a=human --seed=2 --delay=0 --log=none < tests/data/human_quit.txt > /tmp/bs_out.txt; check "человек вышел по q" 0 $?
grep -q "Не понял" /tmp/bs_out.txt; check "  мусор отклонён" 0 $?
grep -q "уже стреляли" /tmp/bs_out.txt; check "  повтор отклонён" 0 $?
grep -q "вышел из боя" /tmp/bs_out.txt; check "  сообщение о выходе" 0 $?
$BIN --a=human --seed=2 --delay=0 --log=none < tests/data/human_all_cells.txt > /tmp/bs_out.txt; check "человек стреляет по всем клеткам" 0 $?
grep -q "Победил" /tmp/bs_out.txt; check "  бой закончился победой" 0 $?
$BIN --a=human --seed=2 --log=none < /dev/null > /dev/null; check "конец ввода - выход без ошибки" 0 $?
$BIN --a=human --games=5 > /dev/null 2>&1; check "human с games>1 отклонён" 1 $?

$BIN --games=100 --delay=0 --seed=1 --log=/tmp/bs_series.log > /tmp/bs_out.txt; check "серия из 100 боёв" 0 $?
grep -q "Сыграно боёв: 100" /tmp/bs_out.txt; check "  сыграно 100" 0 $?
grep -q "Сыграно боёв" /tmp/bs_series.log; check "  итог есть в журнале" 0 $?
$BIN --games=0 > /dev/null 2>&1; check "games=0 отклонён" 1 $?
$BIN --games=100000 --delay=0 --log=none > /dev/null & pid=$!
sleep 1; kill -INT $pid; wait $pid; check "Ctrl+C во время серии" 130 $?

exit $fail
