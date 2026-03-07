import duckdb
import pandas as pd
import plotly.express as px

conn = duckdb.connect("f1.duckdb")

# Load the data
df = conn.query("""
    SELECT *
    FROM position_telemetry
    WHERE EventName = 'Las Vegas Grand Prix' 
      AND DriverNumber = 4 
      AND X != 0 AND Y != 0 AND Z != 0
    ORDER BY Time
""").df()

# Identify consecutive Z values
df["Z_shift"] = df["Z"].shift(1)
df["same_Z"] = df["Z"] == df["Z_shift"]

# Count consecutive same Z occurrences
df["run_id"] = (~df["same_Z"]).cumsum()  # new run when Z changes
run_lengths = df.groupby("run_id").size().rename("run_length")
df = df.merge(run_lengths, left_on="run_id", right_index=True)

# Keep only runs where length <= 10
df_filtered = df[df["run_length"] <= 10].copy()

# Optional: drop helper columns
df_filtered.drop(columns=["Z_shift", "same_Z", "run_id", "run_length"], inplace=True)

# Plot
fig = px.scatter_3d(
    df_filtered,
    x="X",
    y="Y",
    z="Z",
    color="Time",
    color_continuous_scale="Viridis",
    title="Driver 4 Telemetry - Las Vegas GP",
    hover_data=["Time"],
)

fig.update_layout(scene=dict(xaxis_title="X", yaxis_title="Y", zaxis_title="Z"))
fig.show()

