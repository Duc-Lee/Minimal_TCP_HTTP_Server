import argparse
import os
import re
import shutil
import subprocess
import sys
import time


def parse_log(filepath):
    # trich xuat thong so cwnd, ssthresh tu file log (ho tro ca Reno, BBR va Basic)
    if not os.path.exists(filepath):
        print(f"Error: file '{filepath}' khong ton tai")
        sys.exit(1)

    pattern = re.compile(
        r"(?:\[CWND\]\s+tick=(\d+)\s+event=(\S+)|cwnd:\s+t=(\d+)\s+(\S+))\s+cwnd=(\d+)\s+ssthresh=(\d+)"
    )

    events = []
    cwnds = []
    ssthreshs = []
    fr_indices = []
    fr_cwnds = []
    to_indices = []
    to_cwnds = []
    is_bbr = False

    with open(filepath, "r", encoding="utf-8", errors="ignore") as f:
        for line in f:
            m = pattern.search(line)
            if not m:
                continue

            ev = m.group(2) if m.group(1) is not None else m.group(4)
            c = int(m.group(5))
            s = int(m.group(6))

            if ev.startswith("BBR_"):
                is_bbr = True

            idx = len(cwnds)
            cwnds.append(c)
            ssthreshs.append(s)
            events.append(ev)

            if ev in ("FAST_RETRANSMIT", "FAST_RETX", "BBR_FAST_RETX"):
                fr_indices.append(idx)
                fr_cwnds.append(c)
            elif ev in ("TIMEOUT", "BBR_TIMEOUT", "BASIC_TIMEOUT"):
                to_indices.append(idx)
                to_cwnds.append(c)

    if not cwnds:
        print(f"Error: khong tim thay du lieu cwnd trong '{filepath}'")
        sys.exit(1)

    return {
        "events": events,
        "cwnds": cwnds,
        "ssthreshs": ssthreshs,
        "fr_indices": fr_indices,
        "fr_cwnds": fr_cwnds,
        "to_indices": to_indices,
        "to_cwnds": to_cwnds,
        "is_bbr": is_bbr,
    }


def _load_fonts():
    from PIL import ImageFont

    regular_paths = [
        "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf",
        "/usr/share/fonts/truetype/liberation/LiberationSans-Regular.ttf",
    ]
    bold_paths = [
        "/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf",
        "/usr/share/fonts/truetype/liberation/LiberationSans-Bold.ttf",
    ]
    reg = next((p for p in regular_paths if os.path.exists(p)), None)
    bold = next((p for p in bold_paths if os.path.exists(p)), None)
    if reg and bold:
        return (
            ImageFont.truetype(bold, 18),
            ImageFont.truetype(reg, 13),
            ImageFont.truetype(reg, 11),
        )
    dft = ImageFont.load_default()
    return dft, dft, dft


def _draw_dashed_line(draw, p1, p2, fill, width=2, dash=8, gap=5):
    x1, y1 = p1
    x2, y2 = p2
    dx = x2 - x1
    dy = y2 - y1
    dist = (dx * dx + dy * dy) ** 0.5
    if dist < 1:
        return
    steps = int(dist / (dash + gap)) + 1
    for i in range(steps):
        t0 = (i * (dash + gap)) / dist
        t1 = min(1.0, (i * (dash + gap) + dash) / dist)
        if t0 >= 1.0:
            break
        draw.line(
            [(x1 + dx * t0, y1 + dy * t0), (x1 + dx * t1, y1 + dy * t1)],
            fill=fill,
            width=width,
        )


def _plot_with_pil(data, output_png, title):
    from PIL import Image, ImageDraw

    W, H = 1040, 540
    ML, MR, MT, MB = 92, 36, 56, 62
    PW = W - ML - MR
    PH = H - MT - MB

    img = Image.new("RGB", (W, H), "#ffffff")
    draw = ImageDraw.Draw(img)
    f_title, f_label, f_tick = _load_fonts()

    cwnds = data["cwnds"]
    ssthreshs = data["ssthreshs"]
    n = len(cwnds)
    max_y = max(max(cwnds), max(ssthreshs), 65535)
    max_y = int(((max_y + 4999) // 5000) * 5000)

    def px(idx):
        if n <= 1:
            return ML
        return ML + (idx / (n - 1)) * PW

    def py(val):
        return MT + PH - (val / max_y) * PH

    # Grid va truc
    for i in range(6):
        yv = int(max_y * i / 5)
        y = py(yv)
        _draw_dashed_line(draw, (ML, y), (ML + PW, y), "#dddddd", width=1, dash=5, gap=4)
        lbl = f"{yv}"
        bbox = draw.textbbox((0, 0), lbl, font=f_tick)
        draw.text((ML - (bbox[2] - bbox[0]) - 8, y - 7), lbl, fill="#333333", font=f_tick)

    xticks = min(8, max(2, n - 1))
    for i in range(xticks + 1):
        xv = int(round((n - 1) * i / xticks)) if n > 1 else 0
        x = px(xv)
        _draw_dashed_line(draw, (x, MT), (x, MT + PH), "#e5e5e5", width=1, dash=5, gap=4)
        lbl = str(xv)
        bbox = draw.textbbox((0, 0), lbl, font=f_tick)
        draw.text((x - (bbox[2] - bbox[0]) / 2, MT + PH + 8), lbl, fill="#333333", font=f_tick)

    draw.rectangle([ML, MT, ML + PW, MT + PH], outline="#333333", width=2)

    # Tieu de va nhan truc
    tb = draw.textbbox((0, 0), title, font=f_title)
    draw.text(((W - (tb[2] - tb[0])) / 2, 16), title, fill="#111111", font=f_title)
    xl = "Timeline (Event Index)"
    xb = draw.textbbox((0, 0), xl, font=f_label)
    draw.text((ML + (PW - (xb[2] - xb[0])) / 2, H - 28), xl, fill="#222222", font=f_label)
    draw.text((12, MT + PH / 2 - 10), "Bytes", fill="#222222", font=f_label)

    # Duong ssthresh (cam net dut)
    ss_pts = [(px(i), py(ssthreshs[i])) for i in range(n)]
    for i in range(len(ss_pts) - 1):
        _draw_dashed_line(draw, ss_pts[i], ss_pts[i + 1], "#ff7f0e", width=2, dash=8, gap=5)

    # Duong cwnd (xanh duong dam)
    cw_pts = [(px(i), py(cwnds[i])) for i in range(n)]
    if len(cw_pts) >= 2:
        draw.line(cw_pts, fill="#1f77b4", width=3)

    # Diem Fast Retransmit (cham tron do)
    for idx, val in zip(data["fr_indices"], data["fr_cwnds"]):
        x, y = px(idx), py(val)
        r = 5
        draw.ellipse([x - r, y - r, x + r, y + r], fill="#d62728", outline="#ffffff", width=1)

    # Diem Timeout (hinh vuong tim)
    for idx, val in zip(data["to_indices"], data["to_cwnds"]):
        x, y = px(idx), py(val)
        r = 5
        draw.rectangle([x - r, y - r, x + r, y + r], fill="#9467bd", outline="#ffffff", width=1)

    # Hop chu giai (Legend)
    items = [("cwnd", "line", "#1f77b4"), ("ssthresh", "dash", "#ff7f0e")]
    if data["fr_indices"]:
        items.append(("Fast Retransmit", "dot", "#d62728"))
    if data["to_indices"]:
        items.append(("RTO Timeout", "sq", "#9467bd"))

    lx = ML + PW - 188
    ly = MT + 12
    lh = 20 * len(items) + 12
    draw.rectangle([lx, ly, lx + 178, ly + lh], fill="#ffffff", outline="#bbbbbb", width=1)
    for k, (name, kind, col) in enumerate(items):
        cy = ly + 16 + k * 20
        if kind == "line":
            draw.line([(lx + 10, cy), (lx + 34, cy)], fill=col, width=3)
        elif kind == "dash":
            _draw_dashed_line(draw, (lx + 10, cy), (lx + 34, cy), col, width=2, dash=6, gap=4)
        elif kind == "dot":
            draw.ellipse([lx + 18, cy - 4, lx + 26, cy + 4], fill=col)
        else:
            draw.rectangle([lx + 18, cy - 4, lx + 26, cy + 4], fill=col)
        draw.text((lx + 42, cy - 7), name, fill="#222222", font=f_tick)

    img.save(output_png, format="PNG")
    print(f"Saved plot: {output_png}")


def _write_svg(data, output_svg, title):
    W, H = 1000, 500
    ML, MR, MT, MB = 85, 30, 50, 55
    PW, PH = W - ML - MR, H - MT - MB
    cwnds = data["cwnds"]
    ssthreshs = data["ssthreshs"]
    n = len(cwnds)
    max_y = max(max(cwnds), max(ssthreshs), 65535)
    max_y = int(((max_y + 4999) // 5000) * 5000)

    def px(idx):
        return ML if n <= 1 else ML + (idx / (n - 1)) * PW

    def py(val):
        return MT + PH - (val / max_y) * PH

    cw_poly = " ".join(f"{px(i):.1f},{py(cwnds[i]):.1f}" for i in range(n))
    ss_poly = " ".join(f"{px(i):.1f},{py(ssthreshs[i]):.1f}" for i in range(n))

    lines = [
        f'<svg xmlns="http://www.w3.org/2000/svg" width="{W}" height="{H}" viewBox="0 0 {W} {H}">',
        f'<rect width="{W}" height="{H}" fill="#ffffff"/>',
        f'<text x="{W/2}" y="28" text-anchor="middle" font-family="sans-serif" font-size="18" font-weight="bold">{title}</text>',
        f'<rect x="{ML}" y="{MT}" width="{PW}" height="{PH}" fill="none" stroke="#333" stroke-width="1.5"/>',
    ]
    for i in range(6):
        yv = int(max_y * i / 5)
        y = py(yv)
        lines.append(f'<line x1="{ML}" y1="{y:.1f}" x2="{ML+PW}" y2="{y:.1f}" stroke="#ddd" stroke-dasharray="4,4"/>')
        lines.append(f'<text x="{ML-8}" y="{y+4:.1f}" text-anchor="end" font-family="sans-serif" font-size="11">{yv}</text>')

    lines.append(f'<polyline fill="none" stroke="#ff7f0e" stroke-width="1.8" stroke-dasharray="7,5" points="{ss_poly}"/>')
    lines.append(f'<polyline fill="none" stroke="#1f77b4" stroke-width="2.2" points="{cw_poly}"/>')
    for idx, val in zip(data["fr_indices"], data["fr_cwnds"]):
        lines.append(f'<circle cx="{px(idx):.1f}" cy="{py(val):.1f}" r="4.5" fill="#d62728"/>')
    for idx, val in zip(data["to_indices"], data["to_cwnds"]):
        x, y = px(idx), py(val)
        lines.append(f'<rect x="{x-4:.1f}" y="{y-4:.1f}" width="8" height="8" fill="#9467bd"/>')
    lines.append("</svg>")

    with open(output_svg, "w", encoding="utf-8") as f:
        f.write("\n".join(lines))
    print(f"Saved plot: {output_svg}")


def _sample_chart_cell(raw, row_stride, x0, y0, x1, y1):
    # Uu tien net ve co mau sac cao (cwnd/ssthresh/marker/cot) va net dam
    # de khi thu nho ve do phan giai terminal khong bi lu mo tren nen trang.
    best_score = -1
    best_rgb = (255, 255, 255)
    for y in range(y0, y1):
        base = y * row_stride + x0 * 3
        end = y * row_stride + x1 * 3
        for pos in range(base, end, 3):
            r = raw[pos]
            g = raw[pos + 1]
            b = raw[pos + 2]
            mx = r if r >= g and r >= b else (g if g >= b else b)
            mn = r if r <= g and r <= b else (g if g <= b else b)
            score = (mx - mn) * 3 + (765 - (r + g + b))
            if score > best_score:
                best_score = score
                best_rgb = (r, g, b)
    return best_rgb


def render_image_terminal(img_or_path, width=0, preserve_chart=True):
    from PIL import Image

    if isinstance(img_or_path, Image.Image):
        img = img_or_path.convert("RGB")
    else:
        with Image.open(img_or_path) as opened:
            img = opened.convert("RGB")

    src_w, src_h = img.size
    if width and width > 0:
        px_w = max(20, width)
    else:
        cols = shutil.get_terminal_size((100, 30)).columns
        if not sys.stdout.isatty():
            cols = 96
        px_w = max(60, min(cols - 2, 108))

    # Moi ky tu '▀' chua 2 pixel doc nen ti le subpixel gan nhu vuong 1:1
    px_h = max(2, int(round(src_h * (px_w / src_w))))
    if px_h % 2 != 0:
        px_h += 1

    if preserve_chart and src_w >= px_w and src_h >= px_h:
        raw = img.tobytes()
        row_stride = src_w * 3
        grid = []
        for ty in range(px_h):
            y0 = (ty * src_h) // px_h
            y1 = max(y0 + 1, ((ty + 1) * src_h) // px_h)
            row = []
            for tx in range(px_w):
                x0 = (tx * src_w) // px_w
                x1 = max(x0 + 1, ((tx + 1) * src_w) // px_w)
                row.append(_sample_chart_cell(raw, row_stride, x0, y0, x1, y1))
            grid.append(row)
    else:
        resized = img.resize((px_w, px_h), Image.Resampling.LANCZOS)
        grid = [
            [resized.getpixel((tx, ty)) for tx in range(px_w)]
            for ty in range(px_h)
        ]

    lines = []
    for ty in range(0, px_h, 2):
        top_row = grid[ty]
        bot_row = grid[ty + 1]
        parts = []
        for tx in range(px_w):
            r1, g1, b1 = top_row[tx]
            r2, g2, b2 = bot_row[tx]
            parts.append(f"\033[38;2;{r1};{g1};{b1};48;2;{r2};{g2};{b2}m\u2580")
        parts.append("\033[0m")
        lines.append("".join(parts))
    return "\n".join(lines)


def open_image_viewer(img_path):
    # Tu dong mo cua so xem anh PNG rieng tren desktop (Loupe/xdg-open/eog)
    if not (os.environ.get("DISPLAY") or os.environ.get("WAYLAND_DISPLAY")):
        return False
    abs_path = os.path.abspath(img_path)
    for viewer in ("xdg-open", "eog", "gio", "feh", "display"):
        exe = shutil.which(viewer)
        if not exe:
            continue
        cmd = [exe, "open", abs_path] if viewer == "gio" else [exe, abs_path]
        try:
            subprocess.Popen(
                cmd,
                stdin=subprocess.DEVNULL,
                stdout=subprocess.DEVNULL,
                stderr=subprocess.DEVNULL,
                start_new_session=True,
            )
            time.sleep(0.15)
            return True
        except OSError:
            continue
    return False


def print_image_terminal(img_or_path, caption=None, width=0):
    try:
        rendered = render_image_terminal(img_or_path, width=width)
    except ImportError:
        print("(Thieu Pillow nen khong the in anh ra terminal)")
        return
    except Exception as exc:
        print(f"(Khong the in anh '{img_or_path}' ra terminal: {exc})")
        return

    if caption:
        print(f"\n--- {caption} ---")
    elif isinstance(img_or_path, str):
        print(f"\n--- {os.path.basename(img_or_path)} ---")
    print(rendered)


def plot_cwnd(
    data,
    output_png,
    output_svg=None,
    title=None,
    show_gui=True,
    show_terminal=False,
    term_width=0,
):
    if title is None:
        title = (
            "Google BBR Congestion Control"
            if data.get("is_bbr")
            else "TCP Reno Congestion Control (RFC 5681)"
        )

    out_dir = os.path.dirname(output_png)
    if out_dir:
        os.makedirs(out_dir, exist_ok=True)
    if output_svg:
        svg_dir = os.path.dirname(output_svg)
        if svg_dir:
            os.makedirs(svg_dir, exist_ok=True)

    try:
        import matplotlib.pyplot as plt

        cwnds = data["cwnds"]
        ssthreshs = data["ssthreshs"]
        x = range(len(cwnds))

        plt.figure(figsize=(10, 5))
        plt.plot(x, cwnds, label="cwnd", color="#1f77b4", linewidth=2)
        plt.plot(x, ssthreshs, label="ssthresh", color="#ff7f0e", linewidth=1.8, linestyle="--")

        if data["fr_indices"]:
            plt.scatter(data["fr_indices"], data["fr_cwnds"], color="#d62728", s=40, zorder=5, label="Fast Retransmit")
        if data["to_indices"]:
            plt.scatter(data["to_indices"], data["to_cwnds"], color="#9467bd", marker="s", s=45, zorder=5, label="RTO Timeout")

        plt.title(title, fontsize=14, fontweight="bold")
        plt.xlabel("Timeline (Event Index)", fontsize=11)
        plt.ylabel("Window Size (Bytes)", fontsize=11)
        plt.grid(True, linestyle="--", alpha=0.5)
        plt.legend(loc="upper right", frameon=True)
        plt.tight_layout()
        plt.savefig(output_png, dpi=300)
        print(f"Saved plot: {output_png}")
        if output_svg:
            plt.savefig(output_svg)
            print(f"Saved plot: {output_svg}")
        plt.close()
    except ImportError:
        _plot_with_pil(data, output_png, title)
        if output_svg:
            _write_svg(data, output_svg, title)

    if show_gui:
        open_image_viewer(output_png)

    if show_terminal:
        print_image_terminal(output_png, caption=f"{title} ({output_png})", width=term_width)
        max_y = max(max(data["cwnds"]), max(data["ssthreshs"]), 65535)
        max_y = int(((max_y + 4999) // 5000) * 5000)
        print(
            f"  [Truc Y: 0..{max_y} B | Truc X: 0..{len(data['cwnds']) - 1} events | "
            f"Xanh duong=cwnd  Cam=ssthresh  Do=FastRetx({len(data['fr_indices'])})  Tim=RTO({len(data['to_indices'])})]"
        )


def main():
    parser = argparse.ArgumentParser(description="Ve do thi bien thien cwnd TCP Reno / Google BBR")
    parser.add_argument(
        "-i",
        "--input",
        default="doc/week_3/cwnd_trace.log",
        help="Duong dan file trace log (mac dinh: doc/week_3/cwnd_trace.log)",
    )
    parser.add_argument(
        "-o",
        "--output",
        default="doc/week_3/cwnd_plot.png",
        help="Duong dan file anh dau ra (mac dinh: doc/week_3/cwnd_plot.png)",
    )
    parser.add_argument(
        "--svg",
        default=None,
        help="Duong dan file svg dau ra (tuy chon)",
    )
    parser.add_argument(
        "--title",
        default=None,
        help="Tieu de bieu do",
    )
    parser.add_argument(
        "--show",
        nargs="+",
        metavar="PNG",
        help="Mo cua so xem anh (hoac in terminal voi --terminal) cho cac file PNG",
    )
    parser.add_argument(
        "--no-open",
        action="store_true",
        help="Khong tu dong mo cua so xem anh sau khi luu file",
    )
    parser.add_argument(
        "--terminal",
        action="store_true",
        help="In them anh ANSI TrueColor ngay trong terminal",
    )
    parser.add_argument(
        "--width",
        type=int,
        default=0,
        help="Chieu rong anh khi dung --terminal theo so cot ky tu (0 = tu dong)",
    )

    args = parser.parse_args()
    if args.show:
        for path in args.show:
            if not os.path.exists(path):
                print(f"Error: file '{path}' khong ton tai")
                return 1
            if args.terminal:
                print_image_terminal(path, caption=path, width=args.width)
            elif not args.no_open:
                open_image_viewer(path)
        return 0

    data = parse_log(args.input)
    plot_cwnd(
        data,
        args.output,
        args.svg,
        args.title,
        show_gui=not args.no_open,
        show_terminal=args.terminal,
        term_width=args.width,
    )
    return 0


if __name__ == "__main__":
    sys.exit(main())
