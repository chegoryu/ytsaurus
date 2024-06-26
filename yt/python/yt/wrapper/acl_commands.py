"""Permissions commands"""

from .client_impl import YtClient
from .typing_hack import TFormat, TPath
from .common import set_param, YtError
from .driver import make_request, make_formatted_request, set_master_read_params

from typing import (
    Self,
    TypedDict,
)


class TAcl(TypedDict):
    subjects: list[str]
    action: str
    permissions: list[str]


class AclBuilder(object):
    """
    Class to construct acl in a builder fashion.
    """

    def __init__(self, possible_permissions: list[str] | None = None):
        self._possible_permissions = possible_permissions
        self._acl = []

    def ace(self, action: str, permissions: list[str], subjects: list[str]) -> Self:
        if not isinstance(permissions, list):
            permissions = [permissions]
        for permission in permissions:
            if self._possible_permissions is not None and permission not in self._possible_permissions:
                raise YtError(
                    "Only {0} permissions are supported for operation ACL, got '{1}' instead".format(
                        self._possible_permissions, permission
                    )
                )
        if action not in ["allow", "deny"]:
            raise YtError("Action must be either 'allow' or 'deny', got '{0}' instead".format(action))
        self._acl.append(
            {
                "subjects": subjects,
                "action": action,
                "permissions": permissions,
            }
        )
        return self

    def allow(self, permissions: list[str], subjects: list[str]) -> Self:
        return self.ace("allow", permissions, subjects)

    def deny(self, permissions: list[str], subjects: list[str]) -> Self:
        return self.ace("deny", permissions, subjects)

    def build(self) -> list[TAcl]:
        return self._acl


def check_permission(
    user: str,
    permission: str,
    path: TPath,
    format: TFormat | None = None,
    read_from=None,
    cache_sticky_group_size: int | None = None,
    columns: list[str] | None = None,
    client: YtClient | None = None,
):
    """Checks permission for Cypress node.

    :param str user: user login.
    :param str permission: one of ["read", "write", "administer", "create", "use"].
    :return: permission in specified format (YSON by default).

    .. seealso:: `permissions in the docs <https://ytsaurus.tech/docs/en/user-guide/storage/access-control>`_
    """
    params = {"user": user, "permission": permission, "path": path}
    set_master_read_params(params, read_from, cache_sticky_group_size)
    set_param(params, "columns", columns)
    return make_formatted_request("check_permission", params, format=format, client=client)


def add_member(member: str, group: str, client: YtClient | None = None):
    """Adds member to Cypress node group.

    :param str member: member to add.
    :param str group: group to add member to.

    .. seealso:: `permissions in the docs <https://ytsaurus.tech/docs/en/user-guide/storage/access-control>`_
    """
    return make_request("add_member", {"member": member, "group": group}, client=client)


def remove_member(member: str, group: str, client: YtClient | None = None):
    """Removes member from Cypress node group.

    :param str member: member to remove.
    :param str group: group to remove member from.

    .. seealso:: `permissions in the docs <https://ytsaurus.tech/docs/en/user-guide/storage/access-control>`_
    """
    return make_request("remove_member", {"member": member, "group": group}, client=client)
