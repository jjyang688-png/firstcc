import random


def fortune_teller():
    """A random fortune cookie generator."""
    fortunes = [
        "A beautiful, smart, and loving person will be coming into your life.",
        "A dubious friend may be an enemy in camouflage.",
        "A faithful friend is a strong defense.",
        "A fresh start will put you on your way.",
        "A lifetime of happiness lies ahead of you.",
        "A light heart carries you through all the hard times.",
        "A smile is your passport into the hearts of others.",
        "All your hard work will soon pay off.",
        "An inch of time is an inch of gold.",
        "Be on the alert to recognize your prime at whatever time of your life.",
        "Chance favors those in motion.",
        "You will travel to many exotic places in your lifetime.",
    ]

    name = input("Enter your name: ").strip() or "Stranger"
    print(f"\n {random.choice(fortunes)}")
    print(f"  — fortune for {name}\n")


def number_game():
    """A simple number guessing game."""
    target = random.randint(1, 100)
    attempts = 0
    print("\nI'm thinking of a number between 1 and 100...")

    while True:
        try:
            guess = int(input("Your guess: "))
            attempts += 1
            if guess < target:
                print("Higher!")
            elif guess > target:
                print("Lower!")
            else:
                print(f"You got it in {attempts} attempts!")
                break
        except ValueError:
            print("Please enter a valid number.")


def main():
    print("=" * 40)
    print("    Welcome to Mini Python Fun Pack")
    print("=" * 40)
    print("1. Fortune Cookie")
    print("2. Number Guessing Game")
    print("3. Exit")

    choice = input("\nPick an option (1-3): ").strip()

    if choice == "1":
        fortune_teller()
    elif choice == "2":
        number_game()
    elif choice == "3":
        print("Goodbye!")
    else:
        print("Invalid choice!")


if __name__ == "__main__":
    main()
