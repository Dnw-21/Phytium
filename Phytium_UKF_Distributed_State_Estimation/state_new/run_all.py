import subprocess
import sys

def main():
    print('=' * 60)
    print('One-Click Run: UKF Dynamic State Estimation')
    print('3min, Multi-Fault (5.0-5.3s, 15.0-15.3s)')
    print('=' * 60)

    print('\n[Step 1/2] Running terminal_node.py (Data Generation)...')
    print('-' * 40)
    result = subprocess.run([sys.executable, 'terminal_node.py'], capture_output=False)
    if result.returncode != 0:
        print('ERROR: terminal_node.py failed!')
        return

    print('\n[Step 2/2] Running terminal_controller.py (UKF Estimation)...')
    print('-' * 40)
    result = subprocess.run([sys.executable, 'terminal_controller.py'], capture_output=False)
    if result.returncode != 0:
        print('ERROR: terminal_controller.py failed!')
        return

    print('\n' + '=' * 60)
    print('All steps completed successfully!')
    print('=' * 60)

if __name__ == '__main__':
    main()
