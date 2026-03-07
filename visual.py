import duckdb
import plotly.express as px

conn = duckdb.connect("f1.duckdb")

df = conn.query("""
    SELECT *
    FROM position_telemetry
    WHERE EventName = 'Las Vegas Grand Prix' AND DriverNumber = 4 and X != 0 and Y != 0 and Z != 0
""").df()

fig = px.scatter_3d(
    df,
    x="X",
    y="Y",
    z="Z",
    color="Z",
    color_continuous_scale="Viridis",
    title="Driver 4 Telemetry - Australian GP",
    hover_data=["Date"],
)

fig.update_layout(scene=dict(xaxis_title="X", yaxis_title="Y", zaxis_title="Z"))

fig.show()
