<script lang="ts">
  import type { DriverFrame } from '$lib/commands';
  import { TEAM_COLORS_HEX, COMPOUND_COLORS } from '$lib/constants';

  export let drivers: DriverFrame[] = [];
  export let focusedDriver: string | null = null;
  export let abbrMap: Record<string, string> = {};
  export let teamMap: Record<string, string> = {};

  $: driver = drivers.find((d) => d.driver_number === focusedDriver) ?? drivers[0];
  $: teamColor = driver
    ? (TEAM_COLORS_HEX[teamMap[driver.driver_number]] ?? '#888888')
    : '#888888';
  $: compoundColor = driver
    ? (COMPOUND_COLORS[driver.compound] ?? '#888888')
    : '#888888';
  $: speedPct = driver ? Math.min((driver.speed / 360) * 100, 100).toFixed(1) : '0';
  $: throttlePct = driver ? (driver.throttle * 100).toFixed(1) : '0';
  $: brakePct = driver ? (driver.brake * 100).toFixed(1) : '0';
</script>

<div class="telem">
  {#if driver}
    <div class="driver-name" style="color: {teamColor}">
      {abbrMap[driver.driver_number] ?? driver.driver_number}
    </div>

    <div class="bars">
      <div class="bar-group">
        <label>SPEED</label>
        <div class="bar-track">
          <div
            class="bar"
            style="width: {speedPct}%; background: {teamColor}"
          ></div>
        </div>
        <span class="val">{Math.round(driver.speed)}</span>
      </div>

      <div class="bar-group">
        <label>THROTTLE</label>
        <div class="bar-track">
          <div
            class="bar"
            style="width: {throttlePct}%; background: #00ee44"
          ></div>
        </div>
        <span class="val">{Math.round(driver.throttle * 100)}%</span>
      </div>

      <div class="bar-group">
        <label>BRAKE</label>
        <div class="bar-track">
          <div
            class="bar"
            style="width: {brakePct}%; background: #ff3300"
          ></div>
        </div>
        <span class="val">{Math.round(driver.brake * 100)}%</span>
      </div>
    </div>

    <div class="gear-box">
      <div class="gear">{driver.gear}</div>
      <div class="gear-label">GEAR</div>
    </div>

    <div class="tyre-info">
      <div class="tyre-dot" style="background: {compoundColor}"></div>
      <div>
        <div class="tyre-compound" style="color: {compoundColor}">
          {driver.compound}
        </div>
        <div class="tyre-life">{driver.tyre_life} laps</div>
      </div>
    </div>

    {#if driver.drs_active}
      <div class="drs-badge">DRS</div>
    {/if}

    {#if driver.is_in_pit}
      <div class="pit-badge">PIT</div>
    {/if}
  {:else}
    <div class="no-driver">Click a driver to focus</div>
  {/if}
</div>

<style>
  .telem {
    display: flex;
    align-items: center;
    gap: 20px;
    height: 100%;
    flex-shrink: 0;
  }
  .driver-name {
    font-size: 22px;
    font-weight: 800;
    letter-spacing: 0.1em;
    min-width: 60px;
  }
  .bars {
    display: flex;
    flex-direction: column;
    gap: 6px;
    min-width: 200px;
  }
  .bar-group {
    display: flex;
    align-items: center;
    gap: 8px;
  }
  label {
    font-size: 9px;
    color: rgba(255, 255, 255, 0.4);
    letter-spacing: 0.1em;
    width: 60px;
    flex-shrink: 0;
  }
  .bar-track {
    flex: 1;
    height: 8px;
    background: rgba(255, 255, 255, 0.08);
    border-radius: 2px;
    overflow: hidden;
  }
  .bar {
    height: 100%;
    border-radius: 2px;
    transition: width 0.08s linear;
    min-width: 2px;
  }
  .val {
    font-size: 10px;
    color: rgba(255, 255, 255, 0.6);
    width: 35px;
    text-align: right;
    flex-shrink: 0;
  }
  .gear-box {
    text-align: center;
    flex-shrink: 0;
  }
  .gear {
    font-size: 36px;
    font-weight: 900;
    line-height: 1;
  }
  .gear-label {
    font-size: 9px;
    color: rgba(255, 255, 255, 0.35);
    letter-spacing: 0.1em;
  }
  .tyre-info {
    display: flex;
    align-items: center;
    gap: 8px;
    flex-shrink: 0;
  }
  .tyre-dot {
    width: 12px;
    height: 12px;
    border-radius: 50%;
    flex-shrink: 0;
  }
  .tyre-compound {
    font-size: 11px;
    font-weight: 700;
  }
  .tyre-life {
    font-size: 10px;
    color: rgba(255, 255, 255, 0.4);
  }
  .drs-badge {
    background: #00ccff;
    color: #000;
    font-size: 10px;
    font-weight: 800;
    padding: 2px 8px;
    border-radius: 3px;
    letter-spacing: 0.1em;
    flex-shrink: 0;
  }
  .pit-badge {
    background: #ff8800;
    color: #000;
    font-size: 10px;
    font-weight: 800;
    padding: 2px 8px;
    border-radius: 3px;
    letter-spacing: 0.1em;
    flex-shrink: 0;
  }
  .no-driver {
    font-size: 12px;
    color: rgba(255, 255, 255, 0.3);
  }
</style>
