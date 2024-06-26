from contextlib import contextmanager
from typing import Any, Callable, Type


@contextmanager
def ExceptionCatcher(
    exception_types: Type[BaseException] | tuple[Type[BaseException], ...],
    exception_action: Callable[[], None],
    enable: bool = True,
    limit: int = 10,
):
    """If KeyboardInterrupt(s) are caught, does keyboard_interrupt_action."""
    if enable:
        try:
            yield
        except exception_types:
            counter = 0
            while True:
                try:
                    exception_action()
                except exception_types:
                    counter += 1
                    if counter <= limit:
                        continue
                    else:
                        raise
                break
            raise
    else:
        yield


def KeyboardInterruptsCatcher(*args: Any, **kwargs: Any):
    return ExceptionCatcher(KeyboardInterrupt, *args, **kwargs)
