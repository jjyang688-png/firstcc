"""A small collection of utility functions."""


def fibonacci(n: int) -> list[int]:
    """Generate first n Fibonacci numbers."""
    seq = []
    a, b = 0, 1
    for _ in range(n):
        seq.append(a)
        a, b = b, a + b
    return seq


def is_palindrome(s: str) -> bool:
    """Check if a string is a palindrome."""
    cleaned = "".join(c.lower() for c in s if c.isalnum())
    return cleaned == cleaned[::-1]


def fizzbuzz(n: int) -> list[str]:
    """Classic FizzBuzz up to n."""
    result = []
    for i in range(1, n + 1):
        if i % 15 == 0:
            result.append("FizzBuzz")
        elif i % 3 == 0:
            result.append("Fizz")
        elif i % 5 == 0:
            result.append("Buzz")
        else:
            result.append(str(i))
    return result


def word_count(text: str) -> dict[str, int]:
    """Count word occurrences in text."""
    counts = {}
    for word in text.lower().split():
        cleaned = "".join(c for c in word if c.isalnum())
        if cleaned:
            counts[cleaned] = counts.get(cleaned, 0) + 1
    return counts
