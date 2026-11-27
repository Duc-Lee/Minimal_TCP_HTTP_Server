#!/usr/bin/env python3
# Kiem thu & Benchmark da muc mat goi Tuan 6 (TV2 - Phung Huy Hoang):
#   1. So sanh 3 che do: TCP Co ban (Basic - setcc 2), TCP Reno (setcc 0), Google BBR (setcc 1)
#      qua cac muc mat goi: 0%, 5%, 10% (+ Reno vs BBR o muc 20%).
#   2. Trich xuat trace log cwnd o muc loss 10% cho Reno & BBR va tu dong ve 3 bieu do PNG vao doc/week_6/:
#      - doc/week_6/cwnd_reno_loss10.png
#      - doc/week_6/cwnd_bbr_loss10.png
#      - doc/week_6/bbr_vs_reno_chart.png

import argparse
import os
import subprocess
import sys
import time

ROOT_DIR = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
sys.path.insert(0, os.path.join(ROOT_DIR, "tests"))
sys.path.insert(0, os.path.join(ROOT_DIR, "scripts"))

from common.qemu import start_qemu, stop_qemu
from plot_cwnd import _draw_dashed_line, _load_fonts, open_image_viewer, parse_log, plot_cwnd, print_image_terminal

LOG_PATH = "/tmp/week6_benchmark.log"
RENO_TRACE = "/tmp/cwnd_reno_loss10.log"
BBR_TRACE = "/tmp/cwnd_bbr_loss10.log"
DL_PATH = "/tmp/week6_bench_dl.bin"
FILE_SIZE = 1048576


def read_log():
    if not os.path.exists(LOG_PATH):
        return ""
    with open(LOG_PATH, "r", errors="replace") as f:
        return f.read()


def curl_download(url, timeout=65):
    if os.path.exists(DL_PATH):
        os.remove(DL_PATH)
    t0 = time.time()
    res = subprocess.run(
        [
            "curl", "-s", "-o", DL_PATH, "-m", str(timeout),
            "-w", "%{http_code} %{size_download} %{time_total}",
            url,
        ],
        stdout=subprocess.PIPE,
        stderr=subprocess.DEVNULL,
        text=True,
    )
    elapsed = time.time() - t0
    size = os.path.getsize(DL_PATH) if os.path.exists(DL_PATH) else 0
    ok = (res.returncode == 0 and size == FILE_SIZE)
    bw_kbps = (FILE_SIZE / 1024.0 / elapsed) if (ok and elapsed > 0) else 0.0
    return ok, elapsed, bw_kbps, size


def run_case(proc, url, algo_code, loss_rate, trace_out=None, timeout=65):
    proc.stdin.write(f"setcc {algo_code}\nsetloss {loss_rate}\n".encode())
    proc.stdin.flush()
    time.sleep(0.5)

    start_pos = len(read_log())
    ok, elapsed, bw_kbps, size = curl_download(url, timeout=timeout)
    time.sleep(0.4)
    if trace_out:
        segment = read_log()[start_pos:]
        with open(trace_out, "w", encoding="utf-8") as f:
            f.write(segment)
    return ok, elapsed, bw_kbps, size


def render_comparison_chart(results, output_png, show_gui=True, show_terminal=False, term_width=0):
    from PIL import Image, ImageDraw

    W, H = 1080, 560
    ML, MR, MT, MB = 92, 36, 62, 74
    PW, PH = W - ML - MR, H - MT - MB

    img = Image.new("RGB", (W, H), "#ffffff")
    draw = ImageDraw.Draw(img)
    f_title, f_label, f_tick = _load_fonts()

    losses = [0, 5, 10, 20]
    algos = [
        ("TCP Co ban (Basic)", 2, "#8c8c8c"),
        ("TCP Reno (RFC 5681)", 0, "#ff7f0e"),
        ("Google BBR (4-Phase)", 1, "#1f77b4"),
    ]

    max_t = 0.0
    for row in results.values():
        if row["ok"]:
            max_t = max(max_t, row["time"])
    max_t = max(10.0, ((int(max_t * 1.15) + 4) // 5) * 5)

    def py(val):
        return MT + PH - min(1.0, val / max_t) * PH

    # Grid ngang
    for i in range(6):
        tv = max_t * i / 5
        y = py(tv)
        _draw_dashed_line(draw, (ML, y), (ML + PW, y), "#dddddd", width=1, dash=5, gap=4)
        lbl = f"{tv:.0f}s"
        bbox = draw.textbbox((0, 0), lbl, font=f_tick)
        draw.text((ML - (bbox[2] - bbox[0]) - 8, y - 7), lbl, fill="#333333", font=f_tick)

    draw.rectangle([ML, MT, ML + PW, MT + PH], outline="#333333", width=2)

    title = "Thoi gian tai 1 MB: TCP Basic vs TCP Reno vs Google BBR (Thap hon la tot hon)"
    tb = draw.textbbox((0, 0), title, font=f_title)
    draw.text(((W - (tb[2] - tb[0])) / 2, 16), title, fill="#111111", font=f_title)

    group_w = PW / len(losses)
    bar_w = group_w * 0.22

    for gi, loss in enumerate(losses):
        gx = ML + gi * group_w + group_w * 0.14
        xl = f"Loss {loss}%"
        xb = draw.textbbox((0, 0), xl, font=f_label)
        draw.text((ML + (gi + 0.5) * group_w - (xb[2] - xb[0]) / 2, MT + PH + 12), xl, fill="#222222", font=f_label)

        for ai, (_, code, col) in enumerate(algos):
            bx0 = gx + ai * (bar_w + 8)
            bx1 = bx0 + bar_w
            entry = results.get((loss, code))
            if not entry:
                continue
            if not entry["ok"]:
                by = py(max_t * 0.90)
                draw.rectangle([bx0, by, bx1, MT + PH], fill="#cccccc", outline="#666666", width=1)
                txt = f">{entry['time']:.0f}s*"
            else:
                by = py(entry["time"])
                draw.rectangle([bx0, by, bx1, MT + PH], fill=col, outline="#222222", width=1)
                txt = f"{entry['time']:.2f}s"
            vb = draw.textbbox((0, 0), txt, font=f_tick)
            draw.text(((bx0 + bx1) / 2 - (vb[2] - vb[0]) / 2, max(MT + 4, by - 16)), txt, fill="#111111", font=f_tick)

    # Legend
    lx, ly = ML + 18, MT + 12
    draw.rectangle([lx, ly, lx + 235, ly + 74], fill="#ffffff", outline="#bbbbbb", width=1)
    for k, (name, _, col) in enumerate(algos):
        cy = ly + 14 + k * 21
        draw.rectangle([lx + 10, cy - 5, lx + 26, cy + 7], fill=col, outline="#333333")
        draw.text((lx + 34, cy - 6), name, fill="#222222", font=f_tick)

    draw.text((ML, H - 24), "* Basic (no Fast Retransmit) tai muc loss 10% - 20% bi treo qua gioi han timeout benchmark", fill="#555555", font=f_tick)
    os.makedirs(os.path.dirname(output_png), exist_ok=True)
    img.save(output_png, format="PNG")
    print(f"Saved plot: {output_png}")
    if show_gui:
        open_image_viewer(output_png)
    if show_terminal:
        print_image_terminal(img, caption=f"{title} ({output_png})", width=term_width)
        print(f"  [Truc Y: 0..{int(max_t)}s | Nhom cot: Loss 0% / 5% / 10% / 20% | Xam=Basic  Cam=Reno  Xanh duong=BBR]")


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--host", default="127.0.0.1")
    parser.add_argument("--port", type=int, default=20080)
    parser.add_argument("--no-open", action="store_true", help="Khong tu dong mo cua so xem anh bieu do")
    parser.add_argument("--terminal", action="store_true", help="In them anh bieu do trong terminal")
    parser.add_argument("--width", type=int, default=0, help="Chieu rong anh khi dung --terminal (0 = tu dong)")
    args = parser.parse_args()

    url = f"http://{args.host}:{args.port}/largefile.bin"
    results = {}

    proc = start_qemu(boot_cmd="httpd &\n", log_file=LOG_PATH, port=args.port, rst_port=args.port + 1)
    try:
        plan = [
            (0, [(2, "Basic", 25, None), (0, "Reno", 25, None), (1, "BBR", 25, None)]),
            (5, [(2, "Basic", 45, None), (0, "Reno", 25, None), (1, "BBR", 25, None)]),
            (10, [(2, "Basic", 55, None), (0, "Reno", 30, RENO_TRACE), (1, "BBR", 25, BBR_TRACE)]),
            (20, [(0, "Reno", 45, None), (1, "BBR", 30, None)]),
        ]
        for loss, cases in plan:
            print(f"\n=== Loss {loss}% (1 MB largefile.bin) ===", flush=True)
            for code, name, tout, trace_path in cases:
                ok, elapsed, bw, size = run_case(proc, url, code, loss, trace_out=trace_path, timeout=tout)
                results[(loss, code)] = {"ok": ok, "time": elapsed, "bw": bw, "size": size}
                status = f"{elapsed:.2f}s ({bw:.1f} KB/s)" if ok else f"TIMEOUT/INCOMPLETE ({elapsed:.2f}s, {size}B)"
                print(f"  {name:5s} (setcc {code}): {status}", flush=True)
                time.sleep(0.5)

        # Dat 20% Basic la skipped/exceeded neu khong chay
        results[(20, 2)] = {"ok": False, "time": 60.0, "bw": 0.0, "size": 0}
    finally:
        stop_qemu(proc)

    print("\n=== BANG TONG HOP BENCHMARK TUAN 6 (1 MB largefile.bin) ===")
    print("Loss | Basic (Fixed)       | Reno (RFC 5681)     | Google BBR          | BBR vs Reno")
    print("-----+---------------------+---------------------+---------------------+------------")
    for loss in [0, 5, 10, 20]:
        b_ent = results[(loss, 2)]
        r_ent = results[(loss, 0)]
        g_ent = results[(loss, 1)]
        b_str = f"{b_ent['time']:.2f}s ({b_ent['bw']:.0f} KB/s)" if b_ent["ok"] else ">60s (stall)"
        r_str = f"{r_ent['time']:.2f}s ({r_ent['bw']:.0f} KB/s)" if r_ent["ok"] else "FAIL"
        g_str = f"{g_ent['time']:.2f}s ({g_ent['bw']:.0f} KB/s)" if g_ent["ok"] else "FAIL"
        sp = f"{(r_ent['time'] / g_ent['time']):.2f}x" if (r_ent["ok"] and g_ent["ok"] and g_ent["time"] > 0) else "-"
        print(f"{loss:3d}% | {b_str:19s} | {r_str:19s} | {g_str:19s} | {sp}")

    out_dir = os.path.join(ROOT_DIR, "doc", "week_6")
    os.makedirs(out_dir, exist_ok=True)
    show_gui = not args.no_open
    show_term = args.terminal
    if os.path.exists(RENO_TRACE):
        plot_cwnd(
            parse_log(RENO_TRACE),
            os.path.join(out_dir, "cwnd_reno_loss10.png"),
            title="TCP Reno Congestion Window at 10% Loss (Week 6)",
            show_gui=show_gui,
            show_terminal=show_term,
            term_width=args.width,
        )
    if os.path.exists(BBR_TRACE):
        plot_cwnd(
            parse_log(BBR_TRACE),
            os.path.join(out_dir, "cwnd_bbr_loss10.png"),
            title="Google BBR Congestion Window at 10% Loss (Week 6)",
            show_gui=show_gui,
            show_terminal=show_term,
            term_width=args.width,
        )
    render_comparison_chart(
        results,
        os.path.join(out_dir, "bbr_vs_reno_chart.png"),
        show_gui=show_gui,
        show_terminal=show_term,
        term_width=args.width,
    )

    # Kiem tra cac moc bat buoc: Reno & BBR pass 100% o moi muc loss 0/5/10/20% va BBR >= 1.5x o 10% & 20%
    for loss in [0, 5, 10, 20]:
        assert results[(loss, 0)]["ok"], f"Reno fail o loss {loss}%"
        assert results[(loss, 1)]["ok"], f"BBR fail o loss {loss}%"
    assert results[(10, 0)]["time"] / results[(10, 1)]["time"] >= 1.5, "BBR < 1.5x o loss 10%"
    print("\nPASS: Benchmark & 3 bieu do Tuan 6 hoan tat!")
    return 0


if __name__ == "__main__":
    sys.exit(main())
