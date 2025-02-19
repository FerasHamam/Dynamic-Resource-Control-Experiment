import os
import glob
import pandas as pd
import dash
from dash import dcc, html
from dash.dependencies import Input, Output
import plotly.graph_objs as go

# 1. Load CSV Files
# Assume your CSV files follow a naming pattern, e.g. port_1_1_s1-eth1.csv
csv_files = glob.glob("/home/cc/Dynamic-Resource-Control-Experiment/Mininet/port_*.csv")
# Create a dictionary mapping a port identifier to its DataFrame.
dfs = {}
for file in csv_files:
    # Extract an identifier from the filename.
    # For example: "port_1_1_s1-eth1.csv" -> "s1-eth1"
    port_id = os.path.basename(file).split('_')[-1].replace('.csv', '')
    df = pd.read_csv(file, parse_dates=['timestamp'])
    dfs[port_id] = df

# 2. Create the Dash App Layout
app = dash.Dash(__name__)

app.layout = html.Div([
    html.H2("Interactive Port Statistics"),
    html.Div([
        html.Label("Select Primary Port (to highlight):"),
        dcc.Dropdown(
            id="primary-port",
            options=[{"label": port, "value": port} for port in dfs.keys()],
            value=list(dfs.keys())[0],
            clearable=False
        ),
    ], style={'width': '40%', 'display': 'inline-block'}),

    html.Div([
        html.Label("Include Ports (check the ones to plot):"),
        dcc.Checklist(
            id="include-ports",
            options=[{"label": port, "value": port} for port in dfs.keys()],
            value=list(dfs.keys()),
            labelStyle={'display': 'inline-block', 'margin-right': '10px'}
        ),
    ], style={'width': '50%', 'display': 'inline-block', 'verticalAlign': 'top'}),

    dcc.Graph(id="ports-graph")
])

# 3. Update the Graph Based on Selections
@app.callback(
    Output("ports-graph", "figure"),
    Input("primary-port", "value"),
    Input("include-ports", "value")
)
def update_graph(primary_port, include_ports):
    traces = []
    # Plot the primary port (if included) with a special emphasis (e.g., red and thicker)
    if primary_port in include_ports:
        df = dfs[primary_port]
        traces.append(go.Scatter(
            x=df["timestamp"],
            y=df["rx_bytes"],  # or choose another metric like tx_bytes, rx_pkts, etc.
            mode="lines+markers",
            name=f"Primary: {primary_port}",
            line=dict(width=4, color="red")
        ))
    # Plot all the other ports that are selected
    for port, df in dfs.items():
        if port != primary_port and port in include_ports:
            traces.append(go.Scatter(
                x=df["timestamp"],
                y=df["rx_bytes"],
                mode="lines+markers",
                name=port
            ))
    layout = go.Layout(
        title="RX Bytes Over Time",
        xaxis={"title": "Timestamp"},
        yaxis={"title": "RX Bytes"},
        hovermode="closest"
    )
    return {"data": traces, "layout": layout}

if __name__ == "__main__":
    app.run_server(debug=True)
