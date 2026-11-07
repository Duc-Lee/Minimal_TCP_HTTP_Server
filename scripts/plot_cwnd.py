import os
import sys
import re
import argparse
import matplotlib.pyplot as plt


def parse_log(filepath):
    # trich xuat thong so cwnd, ssthresh tu file log
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

    with open(filepath, "r", encoding="utf-8", errors="ignore") as f:
        for line in f:
            m = pattern.search(line)
            if not m:
                continue

            ev = m.group(2) if m.group(1) is not None else m.group(4)
            c = int(m.group(5))
            s = int(m.group(6))

            idx = len(cwnds)
            cwnds.append(c)
            ssthreshs.append(s)
            events.append(ev)

            if ev in ("FAST_RETRANSMIT", "FAST_RETX"):
                fr_indices.append(idx)
                fr_cwnds.append(c)
            elif ev == "TIMEOUT":
                to_indices.append(idx)
                to_cwnds.append(c)

    if not cwnds:
        print(f"Error: khong tim thay du lieu cwnd trong '{filepath}'")
        sys.exit(1)

    return {
        "cwnds": cwnds,
        "ssthreshs": ssthreshs,
        "fr_indices": fr_indices,
        "fr_cwnds": fr_cwnds,
        "to_indices": to_indices,
        "to_cwnds": to_cwnds,
    }


def plot_cwnd(data, output_png, output_svg=None):
    # ve bieu do bang matplotlib
    cwnds = data["cwnds"]
    ssthreshs = data["ssthreshs"]
    x = range(len(cwnds))

    plt.figure(figsize=(10, 5))

    plt.plot(x, cwnds, label="cwnd", color="#1f77b4", linewidth=2)
    plt.plot(
        x,
        ssthreshs,
        label="ssthresh",
        color="#ff7f0e",
        linewidth=1.8,
        linestyle="--",
    )

    if data["fr_indices"]:
        plt.scatter(
            data["fr_indices"],
            data["fr_cwnds"],
            color="#d62728",
            s=40,
            zorder=5,
            label="Fast Retransmit",
        )

    if data["to_indices"]:
        plt.scatter(
            data["to_indices"],
            data["to_cwnds"],
            color="#9467bd",
            marker="s",
            s=45,
            zorder=5,
            label="RTO Timeout",
        )

    plt.title("TCP Reno Congestion Control (RFC 5681)", fontsize=14, fontweight="bold")
    plt.xlabel("Timeline (Event Index)", fontsize=11)
    plt.ylabel("Window Size (Bytes)", fontsize=11)
    plt.grid(True, linestyle="--", alpha=0.5)
    plt.legend(loc="upper right", frameon=True)
    plt.tight_layout()

    out_dir = os.path.dirname(output_png)
    if out_dir:
        os.makedirs(out_dir, exist_ok=True)

    plt.savefig(output_png, dpi=300)
    print(f"Saved plot: {output_png}")

    if output_svg:
        plt.savefig(output_svg)
        print(f"Saved plot: {output_svg}")

    plt.close()


def main():
    parser = argparse.ArgumentParser(description="Ve do thi bien thien cwnd TCP Reno")
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
        default="doc/week_3/cwnd_plot.svg",
        help="Duong dan file svg dau ra (mac dinh: doc/week_3/cwnd_plot.svg)",
    )

    args = parser.parse_args()
    data = parse_log(args.input)
    plot_cwnd(data, args.output, args.svg)


if __name__ == "__main__":
    main()
