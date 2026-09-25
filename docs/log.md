11:46:10.420> [0.009] 2/F: flash probe ok: jedec=c2 28 17 sr=0
11:46:10.420> [0.013] 2/S: probe rc=0 jedec=c2 28 17
11:46:10.420> [0.016] 2/S: tick cfg: PCLK1=160000000 TIM6 PSC=159 ARR=999 -> 1000 us (src=0)
11:46:10.420> [0.023] 2/A: selftest: beep 1920 samples via SAI DMA (no queue, no ext flash)
11:46:11.210> [0.263] 2/A: selftest: done in 240 ms (expected ~240 ms, guard=1161690)
11:46:11.554> [0.570] 2/A: selftest: beep 1920 samples via SAI DMA (no queue, no ext flash)
11:46:11.982> [0.809] 2/A: selftest: done in 239 ms (expected ~240 ms, guard=1161791)
11:46:12.614> [1.117] 2/A: selftest: beep 1920 samples via SAI DMA (no queue, no ext flash)
11:46:13.106> [1.356] 2/A: selftest: done in 239 ms (expected ~240 ms, guard=1161791)
11:46:13.737> [1.664] 2/S: --- vector log on: itm=1 uart=UART4 ---
11:46:13.737> [1.664] 2/S: level=2 mask=ffffffff
11:46:13.737> [1.664] 2/A: audio init
11:46:13.737> [1.664] 2/A: image ok=1 sounds=4
11:46:13.737> [1.664] 2/B: bridge on: uart4(hd=1) <-> usart2, ring=256
11:46:14.424> [1.665] 2/A: boot: state 0, sound #0, loop=1 pause=500 ms
11:46:14.424> <0>[1.677] 2/F: read 29536 b @a8 ok, 7 ms
11:46:14.424> [1.681] 2/A: play #0 'a_gas' 14768 samples @8000 Hz
11:46:33.766> [1.832] 2/S: tick 10 s: tx=+1000 hal=+1832 irq=+1832 cb=+1832 keys=+0
11:46:34.021> [1.841] 2/S: irq load: edges=+0 rx4=+154 rx2=+1 rearm4=+1
11:46:53.798> [1.989] 2/S: tick 10 s: tx=+1000 hal=+157 irq=+157 cb=+157 keys=+0
11:46:54.179> [1.997] 2/S: irq load: edges=+0 rx4=+130 rx2=+0 rearm4=+0
11:47:07.750> [2.111] 2/F: read 29536 b @a8 ok, 1 ms
11:47:07.750> [2.114] 2/A: play #0 'a_gas' 14768 samples @8000 Hz
11:47:10.764> [2.148] 2/A: stop (aborted idx=0)
11:47:10.764> [2.152] 2/A: play #1 'b_click' 480 samples @8000 Hz
11:47:11.035> [2.167] 2/A: play #1 'b_click' 480 samples @8000 Hz
11:47:13.825> [2.197] 2/S: tick 10 s: tx=+1000 hal=+208 irq=+208 cb=+208 keys=+3
11:47:13.825> [2.205] 2/S: irq load: edges=+39 rx4=+361 rx2=+0 rearm4=+0
11:47:14.397> [2.217] 2/A: stop: loop off (state=1)
11:47:14.825> [2.229] 2/F: read 35200 b @77c8 ok, 2 ms
11:47:14.825> [2.233] 2/A: play #2 'c_voice_gas' 17600 samples @8000 Hz
11:47:16.866> [2.261] 2/A: stop (aborted idx=2)
11:47:16.866> [2.264] 2/A: state 2 = silent
11:47:17.744> [2.283] 2/F: read 129678 b @10148 ok, 4 ms
11:47:17.744> [2.287] 2/A: play #3 'd_myvoice' 64839 samples @8000 Hz
11:47:19.987> [2.316] 2/A: stop (aborted idx=3)
11:47:19.987> [2.323] 2/F: read 29536 b @a8 ok, 4 ms
11:47:19.987> [2.327] 2/A: play #0 'a_gas' 14768 samples @8000 Hz
11:47:23.698> [2.365] 2/F: read 29536 b @a8 ok, 1 ms
11:47:23.698> [2.369] 2/A: play #0 'a_gas' 14768 samples @8000 Hz

