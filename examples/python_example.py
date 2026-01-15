import francodb
import sys
import json
import binascii

# --- UI Helpers ---
class UI:
    HEADER = '\033[95m'
    BLUE = '\033[94m'
    GREEN = '\033[92m'
    WARNING = '\033[93m'
    FAIL = '\033[91m'
    ENDC = '\033[0m'
    BOLD = '\033[1m'

    @staticmethod
    def print_error(msg):
        print(f"{UI.FAIL}[ERROR] {msg}{UI.ENDC}")

    @staticmethod
    def print_success(msg):
        print(f"{UI.GREEN}[SUCCESS] {msg}{UI.ENDC}")

    @staticmethod
    def print_json(data_str):
        """Parses and prints pretty JSON."""
        try:
            # If the driver returned a list directly (Binary/Object mode), dump it
            if isinstance(data_str, list):
                print(f"{UI.BLUE}{json.dumps(data_str, indent=4)}{UI.ENDC}")
                print(f"{UI.WARNING}Records: {len(data_str)}{UI.ENDC}")
                return

            # If it's a JSON string
            parsed = json.loads(data_str)
            pretty_output = json.dumps(parsed, indent=4)
            print(f"{UI.BLUE}{pretty_output}{UI.ENDC}")
            if isinstance(parsed, list):
                print(f"{UI.WARNING}Records: {len(parsed)}{UI.ENDC}")
        except (json.JSONDecodeError, TypeError):
            # Fallback for plain text
            print(f"{UI.BLUE}{data_str}{UI.ENDC}")

    @staticmethod
    def print_binary(data):
        """
        Handles Binary Protocol output.
        - If data is a LIST (Rows): Prints them clearly.
        - If data is a STRING (Msg): Prints a hex dump.
        """
        print(f"{UI.BLUE}--- BINARY PROTOCOL RESPONSE ---{UI.ENDC}")

        # 1. Handle List (The Result Set) - This fixes the crash
        if isinstance(data, list):
            print(f"{UI.BOLD}Type: Result Set (Parsed from Binary){UI.ENDC}")
            print(f"Row Count: {len(data)}")
            print("-" * 40)
            for i, row in enumerate(data):
                print(f"[{i:03}] {row}")
            print("-" * 40)
            return

        # 2. Handle String (Simple Messages like "OK") - Hex Dump
        try:
            data_bytes = str(data).encode('utf-8')
            print(f"{UI.BOLD}Type: Raw Message (Hex Dump){UI.ENDC}")

            chunk_size = 16
            for i in range(0, len(data_bytes), chunk_size):
                chunk = data_bytes[i:i+chunk_size]
                hex_part = ' '.join(f'{b:02x}' for b in chunk)
                text_part = ''.join(chr(b) if 32 <= b < 127 else '.' for b in chunk)
                print(f"{i:04x}  {hex_part:<48}  |{text_part}|")

            print(f"{UI.BLUE}--- END DUMP ({len(data_bytes)} bytes) ---{UI.ENDC}")
        except Exception as e:
            print(f"Binary display error: {e}")

def main_app():
    print(f"{UI.HEADER}{UI.BOLD}=== FrancoDB Admin Console ==={UI.ENDC}")

    try:
        # Ensure your francodb.py is in the same folder or installed
        conn = francodb.connect(host="127.0.0.1", port=2501, user="maayn", password="root", database="mydb")
        UI.print_success("Connected!")
    except Exception as e:
        UI.print_error(f"Connection failed: {e}")
        return

    with conn:
        with conn.cursor() as cur:
            while True:
                print(f"\n{UI.BOLD}--- MENU ---{UI.ENDC}")
                print("1. Initialize Database")
                print("2. Add User")
                print("3. View Users (Select Mode)")
                print("4. Delete User")
                print("5. Custom Query")
                print("0. Exit")

                choice = input(f"\n{UI.WARNING}Select > {UI.ENDC}")

                # --- OPTION 1: INIT ---
                if choice == '1':
                    try:
                        cur.execute("2E3MEL GADWAL users (id RAKAM, name GOMLA);")
                        UI.print_success("Table created.")
                    except Exception as e:
                        if "exists" in str(e): UI.print_success("Table already exists.")
                        else: UI.print_error(e)

                # --- OPTION 2: ADD ---
                elif choice == '2':
                    uid = input("ID: ")
                    name = input("Name: ")
                    if uid.isdigit():
                        try:
                            # Note: Ensure your driver supports simple text queries for INSERT
                            cur.execute(f"EMLA GOWA users ELKEYAM ({uid}, '{name}');")
                            UI.print_success("User added.")
                        except Exception as e:
                            UI.print_error(e)

                # --- OPTION 3: VIEW (MODE SELECTION) ---
                elif choice == '3':
                    print("\n[Select Protocol Mode]")
                    print("t - Text   (Standard Table)")
                    print("j - JSON   (Web/API Format)")
                    print("b - Binary (High Performance List)")

                    mode_input = input("Mode [t/j/b] > ").lower().strip()

                    selected_mode = 'text'
                    if mode_input == 'j': selected_mode = 'json'
                    elif mode_input == 'b': selected_mode = 'binary'

                    print(f"Fetching data in '{selected_mode}' mode...")

                    try:
                        # --- FIX: Changed 'HAT EL' to correct Franco SQL '2E5TAR * MEN' ---
                        response = cur.execute("2E5TAR * MEN users;", mode=selected_mode)

                        if selected_mode == 'json':
                            UI.print_json(response)
                        elif selected_mode == 'binary':
                            UI.print_binary(response)
                        else:
                            print(f"{UI.BLUE}--- TEXT RESPONSE ---{UI.ENDC}")
                            print(response)

                    except Exception as e:
                        UI.print_error(e)

                # --- OPTION 4: DELETE ---
                elif choice == '4':
                    uid = input("ID to delete: ")
                    try:
                        cur.execute(f"2EMSA7 MEN users LAMA id = {uid};")
                        UI.print_success("Deleted.")
                    except Exception as e:
                        UI.print_error(e)

                # --- OPTION 5: CUSTOM ---
                elif choice == '5':
                    q = input("FQL Query: ")
                    try:
                        print(cur.execute(q))
                    except Exception as e:
                        UI.print_error(e)

                elif choice == '0':
                    print("Goodbye!")
                    break

if __name__ == "__main__":
    try:
        main_app()
    except KeyboardInterrupt:
        print("\nExiting.")