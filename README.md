Z80 + RP2350B + PICO9918 + SN76489
https://github.com/sasagin/RP2350B_GameConsole

соответствие кнопок на 8-битном NES джойстике 

https://github.com/sasagin/ColecoRP2350B/blob/main/DOC/ColecoJoyPad.pdf

Расположение файлов на СД карте:

"/COL/BIOS/main.rom" - переименовать файл BIOS  в main.rom

"/COL/CART/game.rom" - переименовать файл игры  в game.rom

Добавлен маппер SGM и звук PSG SGM  вывод ШИМ на GPIO45.

Запускаются игры нестабильно после нескольких нажатий на ресет на плате Z80.

Почему-то не сразу определяется PSG.

Диагностика SGM вызывается удерживанием в тчение запуска кнопки #(на джойстике START+A)

Игры можно взять здесь https://github.com/retrobrews/colecovision-games

