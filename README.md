# Omni Pedal 软件

对应提案里的效果链：干声直通、overdrive、distortion、delay、chorus、reverb、Bass Split。切换时做大约 10 ms 的交叉淡化。上电输出静音 3 秒。

旋钮和踏板的读取写好了，要在 Raspberry Pi 上接上 ADS7830 和开关才能用。电脑上先听 `out/` 里的 wav。

## 在这台电脑上

安装过 Cygwin 的 g++ 和 make 之后，双击 `run.bat`，或者：

```bash
cd /cygdrive/c/Users/TT/Desktop/guitar
make test
make demo
```

`make test` 通过就会打印 `all passed`。

`make demo` 会写出 `out/` 里的试听文件：

- `input.wav`、`output.wav`、`output_no_mute.wav`：干声和 3 秒静音
- `overdrive.wav`、`distortion.wav`、`delay.wav`、`chorus.wav`、`reverb.wav`
- `bass_low.wav`：110 Hz，带低八度。`bass_high.wav`：330 Hz，保持原音
- `switch.wav`：每秒换一个效果

处理自己的录音：

```bash
./stage1 --effect overdrive --drive 0.6 my_guitar.wav out/processed.wav
./stage1 --effect bass --time 0.6 --mix 0.8 my_guitar.wav out/bass.wav
```

`--time` 在 Bass Split 里是分界频率，0 大约 80 Hz，1 大约 250 Hz。

## 在 Raspberry Pi 上

Codec Zero 接在 AUX IN / AUX OUT。先让声卡走线路输入和线路输出，不要用板载麦克风那一路。

```bash
sudo apt install g++ make libasound2-dev
make
sudo chrt -f 60 ./stage1 --live hw:CodecZero --gpio 17 --i2c 1
```

没接旋钮时，可以在这个终端里输入 `effect overdrive` 或 `next`。踏板用 GPIO 17，避开 Codec Zero 占用的 2、3、18、19、20、21、23、24、27。五个旋钮接 ADS7830 的通道 0 到 4，地址 `0x48`，顺序是 drive、tone、mix、time、level。

名字不对的话先看 `arecord -l`，再把设备名传进去，例如 `./stage1 --live hw:0`。

程序会打印实际打开的周期。如果软件缓冲上限到了 5 ms 或以上，终端里会有一行 warning。
