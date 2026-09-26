# PIN Lock

A 4-digit PIN check with a limit of 3 attempts, like an ATM or a phone lock screen.

## What it does

- Asks for a PIN until it is correct, or until 3 attempts are used.
- Letters instead of numbers: the attempt counts, and the input stream is reset with `cin.clear()` then emptied with `cin.ignore()`.
- A number that is not 4 digits: the attempt counts.
- A wrong 4-digit PIN: the attempt counts.
- The correct PIN: welcome message, then exit.
- After 3 failed attempts the account is locked.

## Edge cases tested

| Input | Result |
|---|---|
| `1234` | Welcome |
| `abc`, then `1234` | Letters rejected, then welcome |
| `12`, then `1234` | Length rejected, then welcome |
| `5555`, `6666`, `7777` | Locked |
| `a`, `b`, `c` | Locked |
| `12`, `34`, `56` | Locked |
| `99999`, then `1234` | Length rejected, then welcome |
| `12abc`, then `1234` | Length rejected, letters rejected, then welcome |

## Lessons learned

- Check for a failed read first. A failed read stores `0` and leaves the stream broken, so checking the length first reports the wrong error and burns every remaining attempt.
- Every failed attempt must count, including wrong 4-digit guesses. Otherwise an attacker can try all 9000 PINs without ever being locked out.

## Next version

- Show how many attempts are left.
- Check the lock condition once, after all branches.
- Read the PIN as a `std::string` so a leading zero (`0123`) is kept, as real banks do.
