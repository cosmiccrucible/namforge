"""Render the five-band EQ magnitude response from the measured sweep."""

import csv
import sys
from pathlib import Path

import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt

CSV = Path(__file__).resolve().parents[2] / "build-vst" / "bench" / "eqresp.csv"

BANDS = [
    ("Bass",      "low shelf @ 100 Hz   (slope 1.0)", 100.0),
    ("Low Mid",   "peak @ 320 Hz   (Q 0.70)",         320.0),
    ("Mid",       "peak @ 900 Hz   (Q 0.80)",         900.0),
    ("High Mid",  "peak @ 2600 Hz   (Q 0.70)",        2600.0),
    ("Treble",    "high shelf @ 5500 Hz   (slope 1.0)", 5500.0),
]

X_TICKS = [20, 50, 100, 200, 500, 1000, 2000, 5000, 10000, 20000]
X_LABELS = ["20", "50", "100", "200", "500", "1k", "2k", "5k", "10k", "20k"]

BG = "#1e1e1e"
PANEL = "#262626"
GRID_MAJOR = "#3a3a3a"
GRID_MINOR = "#2e2e2e"
TEXT = "#b0b0b0"
TITLE = "#e8e8e8"


def load(path):
    data = {}
    with open(path, newline="") as f:
        for row in csv.DictReader(f):
            key = (int(row["band"]), float(row["gain"]))
            data.setdefault(key, []).append((float(row["freq"]), float(row["db"])))
    for pts in data.values():
        pts.sort()
    return data


def render(data, gains, styles, ylim, title, out):
    fig, axes = plt.subplots(5, 1, figsize=(9.5, 10.5), sharex=True)
    fig.patch.set_facecolor(BG)

    for b, ax in enumerate(axes):
        ax.set_facecolor(PANEL)

        for g in gains:
            pts = data.get((b, g), [])
            if not pts:
                continue
            xs = [p[0] for p in pts]
            ys = [p[1] for p in pts]
            colour, width, label = styles[g]
            ax.semilogx(xs, ys, color=colour, linewidth=width, label=label,
                        solid_capstyle="round")

        ax.axhline(0.0, color="#6a6a6a", linewidth=0.8, zorder=1)
        ax.axvline(BANDS[b][2], color="#8a8a8a", linewidth=0.8,
                   linestyle=(0, (4, 3)), zorder=1)

        ax.set_xlim(20, 20000)
        ax.set_ylim(-ylim, ylim)
        ax.grid(True, which="major", color=GRID_MAJOR, linewidth=0.7)
        ax.grid(True, which="minor", color=GRID_MINOR, linewidth=0.4)
        ax.set_axisbelow(True)

        ax.set_title(f"{BANDS[b][0]} — {BANDS[b][1]}", color=TITLE,
                     fontsize=10.5, loc="left", pad=6)
        ax.set_ylabel("dB", color=TEXT, fontsize=8.5)
        ax.tick_params(colors=TEXT, labelsize=8)
        for spine in ax.spines.values():
            spine.set_color("#444444")

    # One shared legend: per-axis legends collide with the rising curves.
    handles = [plt.Line2D([], [], color=styles[g][0], linewidth=max(1.6, styles[g][1]),
                          label=styles[g][2]) for g in gains]
    leg = fig.legend(handles=handles, loc="upper right", bbox_to_anchor=(0.985, 0.972),
                     ncol=len(gains), frameon=False, fontsize=9.5,
                     handlelength=1.8, columnspacing=1.6)
    for t in leg.get_texts():
        t.set_color("#c8c8c8")

    axes[-1].set_xticks(X_TICKS)
    axes[-1].set_xticklabels(X_LABELS)
    axes[-1].set_xlabel("Frequency (Hz)", color=TEXT, fontsize=9)

    fig.suptitle(title, color="#f0f0f0", fontsize=12.5, y=0.995)
    fig.tight_layout(rect=[0, 0, 1, 0.95])
    fig.savefig(out, dpi=150, facecolor=fig.get_facecolor())
    plt.close(fig)
    print("wrote", out)


def main():
    data = load(CSV)

    render(
        data,
        gains=[6.0, 3.0, -3.0, -6.0],
        styles={
            6.0:  ("#e85a4a", 2.2, "+6 dB"),
            3.0:  ("#e8a04a", 1.4, "+3 dB"),
            -3.0: ("#4a9ae8", 1.4, "-3 dB"),
            -6.0: ("#4a5ae8", 2.2, "-6 dB"),
        },
        ylim=8.0,
        title="Five-band inter-stage EQ — magnitude response (per band, other bands at 0 dB)",
        out=r"D:\Coding\NAMHISEModule\docs\eq-response.png",
    )

    render(
        data,
        gains=[0.6, 0.3, -0.3, -0.6],
        styles={
            0.6:  ("#e85a4a", 2.2, "+0.6 dB"),
            0.3:  ("#e8a04a", 1.4, "+0.3 dB"),
            -0.3: ("#4a9ae8", 1.4, "-0.3 dB"),
            -0.6: ("#4a5ae8", 2.2, "-0.6 dB"),
        },
        ylim=1.0,
        title="Five-band inter-stage EQ — magnitude response at ±0.3 / ±0.6 dB (note the ±1 dB scale)",
        out=r"D:\Coding\NAMHISEModule\docs\eq-response-small.png",
    )


if __name__ == "__main__":
    main()
