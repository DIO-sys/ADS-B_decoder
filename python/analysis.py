import sqlite3
import matplotlib.pyplot as plt
import sys
import os

DB_PATH = 'adsb_log.db'

def main():
    if not os.path.exists(DB_PATH):
        print(f"No database found at {DB_PATH}")
        print("Run logger.py first to collect data.")
        return

    db = sqlite3.connect(DB_PATH)

    # Check record count
    count = db.execute('SELECT COUNT(*) FROM records').fetchone()[0]
    print(f"Total records: {count}")
    if count == 0:
        print("No data to analyze.")
        return

    fig, axes = plt.subplots(2, 2, figsize=(14, 10))
    fig.suptitle('ADS-B Decoder Analysis', fontsize=16, fontweight='bold')

    # 1. Message rate over time
    ax = axes[0][0]
    rows = db.execute(
        'SELECT received_at FROM records ORDER BY received_at'
    ).fetchall()
    times = [r[0] for r in rows]
    if len(times) > 1:
        t0 = times[0]
        # Count messages per second
        bins = {}
        for t in times:
            sec = int(t - t0)
            bins[sec] = bins.get(sec, 0) + 1
        seconds = sorted(bins.keys())
        rates = [bins[s] for s in seconds]
        ax.bar(seconds, rates, width=1.0, color='#00cc66', alpha=0.8)
    ax.set_title('Message Rate Over Time')
    ax.set_xlabel('Seconds')
    ax.set_ylabel('Messages/sec')
    ax.grid(True, alpha=0.3)

    # 2. Altitude distribution
    ax = axes[0][1]
    rows = db.execute(
        'SELECT altitude_ft FROM records WHERE altitude_ft > 0'
    ).fetchall()
    alts = [r[0] for r in rows]
    if alts:
        ax.hist(alts, bins=40, color='#3399ff', alpha=0.8, edgecolor='#1a1a2e')
    ax.set_title('Altitude Distribution')
    ax.set_xlabel('Altitude (ft)')
    ax.set_ylabel('Count')
    ax.grid(True, alpha=0.3)

    # 3. Unique aircraft over time
    ax = axes[1][0]
    rows = db.execute(
        'SELECT received_at, icao_address FROM records ORDER BY received_at'
    ).fetchall()
    if rows:
        t0 = rows[0][0]
        seen = {}
        times_plot = []
        counts_plot = []
        unique = set()
        for r in rows:
            sec = int(r[0] - t0)
            unique.add(r[1])
            if sec not in seen:
                seen[sec] = True
                times_plot.append(sec)
                counts_plot.append(len(unique))
        ax.plot(times_plot, counts_plot, color='#ff6633', linewidth=2)
    ax.set_title('Unique Aircraft Over Time')
    ax.set_xlabel('Seconds')
    ax.set_ylabel('Cumulative Aircraft')
    ax.grid(True, alpha=0.3)

    # 4. Signal power distribution
    ax = axes[1][1]
    rows = db.execute(
        'SELECT signal_power FROM records WHERE signal_power < 0'
    ).fetchall()
    powers = [r[0] for r in rows]
    if powers:
        ax.hist(powers, bins=40, color='#ff3366', alpha=0.8, edgecolor='#1a1a2e')
    ax.set_title('Signal Power Distribution')
    ax.set_xlabel('Power (dBFS)')
    ax.set_ylabel('Count')
    ax.grid(True, alpha=0.3)

    plt.tight_layout()
    plt.savefig('adsb_analysis.png', dpi=150, bbox_inches='tight')
    print(f"Saved analysis plot to adsb_analysis.png")
    plt.show()

    db.close()

if __name__ == '__main__':
    main()