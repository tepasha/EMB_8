# ESP32-S3 Console Oscilloscope (ADC DMA → UART)

PlatformIO / ESP-IDF project for `esp32-s3-devkitc-1`.

## What it does

- Continuously samples one ADC channel using the **ADC continuous (DMA) driver**
  (`adc_continuous_*` API) — the ADC peripheral writes conversion results into
  RAM via GDMA without per-sample CPU intervention.
- Every ~100 ms, computes over the collected samples:
  - **Vmin** — lowest voltage seen
  - **Vmax** — highest voltage seen
  - **Vpp**  — peak-to-peak (Vmax − Vmin)
  - **Vavg** — average voltage
- Prints the line to the console over UART using the IDF UART driver's
  buffered, interrupt-driven TX path (`uart_write_bytes` with a ring buffer),
  so transmission runs in the background instead of blocking the sampling task.

## Wiring

Connect the signal you want to observe to **GPIO5** (ADC1 channel 4) on the
DevKitC-1 board, referenced to the board's GND. Keep the signal within
0–3.3 V (the code uses 12 dB attenuation, i.e. full 0–3.3 V range).

## Build & flash

```bash
pio run -t upload
pio device monitor -b 921600
```

The monitor baud rate must match `UART_BAUD` in `src/main.c` (921600).

## Tuning

In `src/main.c`:
- `ADC_CHANNEL_USED` — change the ADC1 channel/GPIO being sampled.
- `ADC_SAMPLE_FREQ_HZ` — sampling rate (default 20 kSPS).
- `REPORT_EVERY_N_SAMPLES` — how many samples are averaged per printed line
  (default: 1/10 s worth of samples, i.e. ~10 updates/second).

## A note on "UART DMA"

The ESP32-S3 UART peripheral has a small hardware FIFO and is driven by the
IDF UART driver via interrupts plus a software ring buffer — that's what
`uart_driver_install()` / `uart_write_bytes()` give you, and it's the
standard, supported way to do buffered/non-blocking UART TX in ESP-IDF.
True memory-to-peripheral UART DMA exists only via the low-level UHCI2
controller, which ESP-IDF does not currently expose through a stable public
driver API, so it isn't used here. The ADC side, by contrast, uses genuine
hardware DMA (GDMA) end-to-end.
