import duckdb
import pandas as pd
import numpy as np
import plotly.graph_objects as go

conn = duckdb.connect("f1.duckdb")

# Official 2024 team colors
TEAM_COLORS = {
    "McLaren": "#FF8000",
    "Ferrari": "#E8002D",
    "Red Bull Racing": "#3671C6",
    "Mercedes": "#27F4D2",
    "Aston Martin": "#229971",
    "Alpine": "#FF87BC",
    "Williams": "#64C4FF",
    "Racing Bulls": "#6692FF",
    "Kick Sauber": "#52E252",
    "Haas F1 Team": "#B6BABD",
}

# ── Load driver info ──────────────────────────────────────────────────────────
print("Loading data...")
drivers_df = conn.query("""
    SELECT DISTINCT DriverNumber, Driver, Team
    FROM laps
    WHERE EventName = 'Las Vegas Grand Prix' AND Session = 'R'
    ORDER BY CAST(DriverNumber AS INT)
""").df()

driver_map = {
    str(r.DriverNumber): {
        "abbr": r.Driver,
        "team": r.Team,
        "color": TEAM_COLORS.get(r.Team, "#FFFFFF"),
    }
    for r in drivers_df.itertuples()
}
all_drivers = sorted(driver_map, key=lambda x: int(x))

# ── Speed heatmap (background) ────────────────────────────────────────────────
heatmap_df = conn.query("""
    SELECT
        ROUND(p.X / 50) * 50  AS XBin,
        ROUND(p.Y / 50) * 50  AS YBin,
        AVG(c.Speed)           AS AvgSpeed,
        COUNT(*)               AS n
    FROM position_telemetry p
    ASOF JOIN car_telemetry c
        ON  p.DriverNumber = c.DriverNumber
        AND p.EventName    = c.EventName
        AND p.Session      = c.Session
        AND p.SessionTime >= c.SessionTime
    WHERE p.EventName = 'Las Vegas Grand Prix'
      AND p.Session   = 'R'
      AND p.X != 0 AND p.Y != 0
    GROUP BY XBin, YBin
    HAVING COUNT(*) > 5
""").df()

# ── Sampled animation data (10-second buckets) ────────────────────────────────
SAMPLE = 10
anim_df = conn.query(f"""
    SELECT
        p.DriverNumber,
        CAST(ROUND(p.SessionTime / {SAMPLE}) * {SAMPLE} AS INT) AS T,
        arg_max(p.X, p.SessionTime)      AS X,
        arg_max(p.Y, p.SessionTime)      AS Y,
        AVG(c.Speed)                     AS Speed,
        arg_max(c.nGear, c.SessionTime)  AS Gear,
        arg_max(c.DRS,   c.SessionTime)  AS DRS
    FROM position_telemetry p
    ASOF JOIN car_telemetry c
        ON  p.DriverNumber = c.DriverNumber
        AND p.EventName    = c.EventName
        AND p.Session      = c.Session
        AND p.SessionTime >= c.SessionTime
    WHERE p.EventName = 'Las Vegas Grand Prix'
      AND p.Session   = 'R'
      AND p.X != 0 AND p.Y != 0
    GROUP BY p.DriverNumber, T
    ORDER BY T, p.DriverNumber
""").df()

print(f"  {anim_df.T.nunique()} animation frames, {len(heatmap_df)} heatmap cells")

# Build fast position lookup
pos_lookup = {}
for row in anim_df.itertuples(index=False):
    pos_lookup[(int(row.T), str(row.DriverNumber))] = row

time_slots = sorted(anim_df["T"].unique())
TRAIL = 10  # trail length in frames (~100 s)


def build_frame_data(t_idx):
    t = time_slots[t_idx]
    trail_slice = time_slots[max(0, t_idx - TRAIL) : t_idx + 1]

    curr_x, curr_y, curr_colors, curr_text = [], [], [], []
    curr_custom = []
    trail_x, trail_y = [], []

    for drv in all_drivers:
        info = driver_map[drv]
        row = pos_lookup.get((int(t), drv))
        if row:
            curr_x.append(float(row.X))
            curr_y.append(float(row.Y))
            spd = float(row.Speed) if not np.isnan(row.Speed) else 0.0
            gear = int(row.Gear) if not np.isnan(row.Gear) else 0
            drs_on = int(row.DRS) in (10, 12, 14)
            curr_custom.append((spd, gear, "DRS" if drs_on else ""))
        else:
            curr_x.append(None)
            curr_y.append(None)
            curr_custom.append((0, 0, ""))
        curr_colors.append(info["color"])
        curr_text.append(info["abbr"])

        # Trail
        txs, tys = [], []
        for tt in trail_slice:
            tr = pos_lookup.get((int(tt), drv))
            if tr:
                txs.append(float(tr.X))
                tys.append(float(tr.Y))
        if txs:
            trail_x.extend(txs)
            trail_y.extend(tys)
            trail_x.append(None)
            trail_y.append(None)

    return curr_x, curr_y, curr_colors, curr_text, curr_custom, trail_x, trail_y


# ── Build figure ──────────────────────────────────────────────────────────────
print("Building figure...")

# Compute colorscale bounds
speed_min = heatmap_df["AvgSpeed"].quantile(0.02)
speed_max = heatmap_df["AvgSpeed"].quantile(0.98)
marker_sz = np.sqrt(heatmap_df["n"].clip(upper=2000) / 2000) * 14 + 6

fig = go.Figure()

# Trace 0: speed heatmap (static)
fig.add_trace(
    go.Scatter(
        x=heatmap_df["XBin"],
        y=heatmap_df["YBin"],
        mode="markers",
        marker=dict(
            size=marker_sz,
            color=heatmap_df["AvgSpeed"],
            cmin=speed_min,
            cmax=speed_max,
            colorscale=[
                [0.00, "#0d0221"],
                [0.15, "#1a0a6e"],
                [0.35, "#0066cc"],
                [0.55, "#00cc88"],
                [0.75, "#ffcc00"],
                [1.00, "#ff2200"],
            ],
            showscale=True,
            colorbar=dict(
                title=dict(text="Speed (km/h)", font=dict(color="white", size=12)),
                tickfont=dict(color="white", size=10),
                x=1.01,
                thickness=12,
                len=0.6,
            ),
            opacity=0.9,
            symbol="square",
        ),
        showlegend=False,
        hovertemplate="Avg speed: %{marker.color:.0f} km/h<extra></extra>",
        name="Speed",
    )
)

# Traces 1…20: per-driver trails (one trace each for team color)
# We'll update them all per frame
init_cx, init_cy, init_col, init_txt, init_cust, init_tx, init_ty = build_frame_data(0)

# Trace 1: trails (all drivers combined)
fig.add_trace(
    go.Scatter(
        x=init_tx,
        y=init_ty,
        mode="lines",
        line=dict(color="rgba(255,255,255,0.18)", width=1.5),
        showlegend=False,
        hoverinfo="skip",
        name="_trails",
    )
)

# Trace 2: current car positions
fig.add_trace(
    go.Scatter(
        x=init_cx,
        y=init_cy,
        mode="markers+text",
        marker=dict(
            size=14,
            color=init_col,
            line=dict(color="white", width=1.5),
            symbol="circle",
        ),
        text=init_txt,
        textposition="top center",
        textfont=dict(color="white", size=9, family="monospace"),
        customdata=init_cust,
        hovertemplate=(
            "<b>%{text}</b><br>"
            "Speed: %{customdata[0]:.0f} km/h<br>"
            "Gear: %{customdata[1]}<br>"
            "%{customdata[2]}<extra></extra>"
        ),
        showlegend=False,
        name="Cars",
    )
)

# ── Animation frames ──────────────────────────────────────────────────────────
print("Building frames (this takes ~30 s)...")
frames = []
for i, t in enumerate(time_slots):
    if i % 100 == 0:
        print(f"  {i}/{len(time_slots)}")
    cx, cy, col, txt, cust, tx, ty = build_frame_data(i)
    mm, ss = divmod(int(t), 60)
    frames.append(
        go.Frame(
            data=[
                go.Scatter(x=tx, y=ty),  # trace 1
                go.Scatter(x=cx, y=cy, marker=dict(color=col), customdata=cust),  # trace 2
            ],
            traces=[1, 2],
            name=str(t),
            layout=go.Layout(
                annotations=[
                    dict(
                        x=0.01,
                        y=0.99,
                        xref="paper",
                        yref="paper",
                        text=f"⏱ {mm:02d}:{ss:02d}",
                        showarrow=False,
                        font=dict(size=18, color="white", family="monospace"),
                        bgcolor="rgba(0,0,0,0.55)",
                        bordercolor="rgba(255,255,255,0.25)",
                        borderwidth=1,
                        borderpad=6,
                        align="left",
                    )
                ]
            ),
        )
    )

fig.frames = frames

# ── Slider steps ─────────────────────────────────────────────────────────────
slider_steps = []
for t in time_slots:
    mm, ss = divmod(int(t), 60)
    slider_steps.append(
        dict(
            args=[
                [str(t)],
                dict(frame=dict(duration=120, redraw=False), mode="immediate", transition=dict(duration=0)),
            ],
            label=f"{mm:02d}:{ss:02d}" if ss == 0 else "",
            method="animate",
        )
    )

# ── Layout ────────────────────────────────────────────────────────────────────
fig.update_layout(
    title=dict(
        text="2024 Las Vegas Grand Prix — Race Replay",
        font=dict(size=20, color="white", family="monospace"),
        x=0.5,
        y=0.97,
    ),
    paper_bgcolor="#03010a",
    plot_bgcolor="#03010a",
    xaxis=dict(
        showgrid=False,
        showticklabels=False,
        zeroline=False,
        scaleanchor="y",
        scaleratio=1,
    ),
    yaxis=dict(showgrid=False, showticklabels=False, zeroline=False),
    height=750,
    margin=dict(l=10, r=60, t=60, b=110),
    updatemenus=[
        dict(
            type="buttons",
            showactive=False,
            x=0.5,
            y=-0.08,
            xanchor="center",
            yanchor="top",
            direction="left",
            buttons=[
                dict(
                    label="▶  Play",
                    method="animate",
                    args=[
                        None,
                        dict(
                            frame=dict(duration=120, redraw=False),
                            fromcurrent=True,
                            transition=dict(duration=60, easing="linear"),
                        ),
                    ],
                ),
                dict(
                    label="⏸  Pause",
                    method="animate",
                    args=[[None], dict(frame=dict(duration=0, redraw=False), mode="immediate")],
                ),
            ],
            font=dict(color="white", size=13, family="monospace"),
            bgcolor="rgba(255,255,255,0.08)",
            bordercolor="rgba(255,255,255,0.3)",
            borderwidth=1,
            pad=dict(r=10, l=10, t=5, b=5),
        )
    ],
    sliders=[
        dict(
            active=0,
            steps=slider_steps,
            x=0.05,
            y=0.0,
            len=0.9,
            currentvalue=dict(
                prefix="Race time  ",
                visible=True,
                xanchor="center",
                font=dict(color="white", size=12, family="monospace"),
            ),
            font=dict(color="rgba(255,255,255,0.5)", size=9),
            bgcolor="rgba(255,255,255,0.06)",
            bordercolor="rgba(255,255,255,0.2)",
            tickcolor="rgba(255,255,255,0.3)",
            activebgcolor="rgba(255,255,255,0.3)",
        )
    ],
    annotations=[
        dict(
            x=0.01,
            y=0.99,
            xref="paper",
            yref="paper",
            text="⏱ 00:00",
            showarrow=False,
            font=dict(size=18, color="white", family="monospace"),
            bgcolor="rgba(0,0,0,0.55)",
            bordercolor="rgba(255,255,255,0.25)",
            borderwidth=1,
            borderpad=6,
        )
    ],
)

# ── Team legend ───────────────────────────────────────────────────────────────
# Inject invisible scatter traces just for the legend
teams_seen = set()
for drv in all_drivers:
    info = driver_map[drv]
    if info["team"] not in teams_seen:
        teams_seen.add(info["team"])
        fig.add_trace(
            go.Scatter(
                x=[None],
                y=[None],
                mode="markers",
                marker=dict(size=10, color=info["color"]),
                name=f"{info['team']}",
                showlegend=True,
            )
        )

fig.update_layout(
    legend=dict(
        x=1.06,
        y=0.5,
        xanchor="left",
        bgcolor="rgba(0,0,0,0.5)",
        bordercolor="rgba(255,255,255,0.2)",
        borderwidth=1,
        font=dict(color="white", size=10, family="monospace"),
    )
)

print("Saving HTML...")
fig.write_html("las_vegas_race_replay.html", include_plotlyjs="cdn")
print("Done! → las_vegas_race_replay.html")
