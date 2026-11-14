# Home Assistant dashboard

A ready-made electricity dashboard for the entities this receiver publishes
(`sensor.electricity_meter_*`). The files come from
[mp3llll/iec-p1-esphome-3phase](https://github.com/mp3llll/iec-p1-esphome-3phase) (MIT).

| File | What it is |
|------|------------|
| `configuration.yaml` | Helper sensors: daily and monthly `utility_meter`, baseload, peak today, comparison with yesterday, cost, month forecast |
| `dashboard-views.yaml` | The full dashboard. Paste it into the **Raw configuration editor** |
| `dashboard.yaml` | The same dashboard as a single card, for a **Manual** card on an existing dashboard |

## Requirements

- The receiver is running and its entities are **available** in Home Assistant (Settings,
  Devices & services, MQTT). Check `p1lora/a101/bridge` and `p1lora/a101/meter` are `online`.
- Three custom cards that are not part of Home Assistant:

| Card | Used for |
|------|----------|
| [Mushroom](https://github.com/piitaya/lovelace-mushroom) | Title and info tiles |
| [ApexCharts Card](https://github.com/RomRider/apexcharts-card) | 24-hour and daily charts |
| [card-mod](https://github.com/thomasloven/lovelace-card-mod) | Right-to-left layout. Provides `custom:mod-card` |

They are installed through HACS, the community store.

## 1. Install HACS

Skip this if HACS already appears in your sidebar. The commands below follow the official guide
at <https://hacs.xyz/docs/use/download/download/>; check it if a step fails.

**Home Assistant in Docker** (replace `homeassistant` with your container name; find it with
`docker ps`):

```bash
docker exec -it homeassistant bash
wget -O - https://get.hacs.xyz | bash -
exit
docker restart homeassistant
```

The config folder (`/config`) must be a persistent volume, or HACS disappears when the
container is recreated. On Home Assistant OS use the **Terminal & SSH** add-on and run the same
`wget` line.

Then add the integration:

1. Hard-refresh the browser (Ctrl+F5).
2. Settings, Devices & services, **Add integration**, search for **HACS**.
3. Accept the acknowledgements and authorise with a free GitHub account (open the link, enter the
   code).
4. **HACS** now appears in the sidebar.

## 2. Install the three cards

HACS starts empty. You have to add each repository:

1. Open **HACS**, then **Explore & download repositories**.
2. Search for **Mushroom**, open it, click **Download**.
3. Repeat for **ApexCharts Card** and **card-mod**.
4. Hard-refresh the browser (Ctrl+F5). Restart Home Assistant if HACS asks.

Check that the resources are registered: in your profile turn on **Advanced mode**, then
Settings, Dashboards, three-dot menu, **Resources**. You should see JavaScript module entries for
all three (for example `/hacsfiles/lovelace-card-mod/card-mod.js`). HACS normally adds them. If
one is missing, add it with **Add resource** and type **JavaScript module**.

## 3. Add the helper sensors

Merge `configuration.yaml` into your own `configuration.yaml`. It adds `utility_meter:`,
`sensor:` and `template:` sections. If you already have one of these keys, add the entries to the
existing key instead of repeating it (a duplicate key is an error).

Then run Developer tools, YAML, **Check configuration**, and restart Home Assistant.

## 4. Add the dashboard

1. Settings, Dashboards, **Add dashboard**, tick **Show in sidebar**, then open it.
2. Click the pencil icon, the three-dot menu, then **Raw configuration editor**.
3. Replace everything with the contents of `dashboard-views.yaml` and save.

To add it to an existing dashboard instead, add a **Manual** card and paste `dashboard.yaml`.
Do not paste `dashboard.yaml` into the raw editor: it has no `views:` key and fails with
"Expected an array value, but received: undefined".

## 5. Adjust

- **Price.** `Electricity Price` uses `price_with_vat = 0.6352` ILS/kWh and a 0% discount.
  `Electricity Forecast Month` uses `fixed_charge = 0`. Change them in `configuration.yaml` to
  match your tariff and bill.
- **Gauge limit.** The load gauge uses `max: 17` (3x25 A). Use `27` for 3x40 A.

## Troubleshooting

| Symptom | Cause and fix |
|---------|---------------|
| `Your configuration is not valid ... views -- Expected an array value` | You pasted `dashboard.yaml` into the raw editor. Paste `dashboard-views.yaml` |
| "Configuration error" card | A custom card is missing. The message names it; install it in HACS and hard-refresh |
| `Custom element doesn't exist: mod-card` | card-mod is not installed or its resource is not registered |
| HACS page is empty | Normal on first start. Click **Explore & download repositories** and search by name |
| HACS missing from the integration list | The install did not land in `/config/custom_components/hacs`, or Home Assistant was not restarted |
| Entities show `unavailable` | `p1lora/a101/bridge` or `meter` is missing or not `online`. Flash the current receiver firmware, which republishes them every 30 s |
| "Comparison to yesterday" says no data | Normal until the statistics sensors hold about a day of history |
| Cost cards show 0 | `sensor.electricity_daily` and `sensor.electricity_monthly` start at 0 and count from when you added `configuration.yaml` |
