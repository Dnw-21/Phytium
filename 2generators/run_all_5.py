"""
One-click runner: Terminal Node → Terminal Controller
Runs the full pipeline for the 5-bus Overbye system.

Usage:
    python run_all_5.py
"""

import subprocess
import sys
import os


def main():
    print('=' * 60)
    print('One-Click Run: IEEE 5-Bus Overbye System UKF DSE')
    print('=' * 60)

    script_dir = os.path.dirname(os.path.abspath(__file__))

    # Step 1: Terminal Node (data generation)
    print('\n' + '=' * 60)
    print('PHASE 1: Terminal Node - Data Generation')
    print('=' * 60)
    result1 = subprocess.run(
        [sys.executable, os.path.join(script_dir, 'terminal_node_5.py')],
        cwd=script_dir
    )
    if result1.returncode != 0:
        print('\nERROR: Terminal node failed!')
        sys.exit(1)

    # Step 2: Terminal Controller (UKF estimation)
    print('\n' + '=' * 60)
    print('PHASE 2: Terminal Controller - UKF Estimation')
    print('=' * 60)
    result2 = subprocess.run(
        [sys.executable, os.path.join(script_dir, 'terminal_controller_5.py')],
        cwd=script_dir
    )
    if result2.returncode != 0:
        print('\nERROR: Controller failed!')
        sys.exit(1)

    print('\n' + '=' * 60)
    print('All steps completed successfully!')
    print('Check the output PNG files for UKF results.')
    print('=' * 60)


if __name__ == '__main__':
    main()
