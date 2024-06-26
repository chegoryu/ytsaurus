from contextlib import contextmanager

from typing import (
    ContextManager,
)

from .client_impl import (
    YtClient,
)
from .typing_hack import (
    TPath,
    TAttributes,
)


@contextmanager
def TempTable(
    path: TPath | None = None,
    prefix: str | None = None,
    attributes: TAttributes | None = None,
    expiration_timeout: int | None = None,
    client: YtClient | None = None,
) -> ContextManager[str]:
    """Creates temporary table in given path with given prefix on scope enter and \
       removes it on scope exit.

       .. seealso:: :func:`create_temp_table <yt.wrapper.table_commands.create_temp_table>`
    """
    from .cypress_commands import remove
    from .table_commands import create_temp_table

    table = create_temp_table(path, prefix, attributes, expiration_timeout, client=client)
    try:
        yield table
    finally:
        remove(table, force=True, client=client)
