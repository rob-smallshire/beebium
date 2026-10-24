# Copyright 2026 Robert Smallshire <robert@smallshire.org.uk>
#
# This file is part of Beebium.
#
# Beebium is free software: you can redistribute it and/or modify it under the terms of the
# GNU General Public License as published by the Free Software Foundation, either version 3 of the
# License, or (at your option) any later version. Beebium is distributed in the hope that it will
# be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
# FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for more details.
# You should have received a copy of the GNU General Public License along with Beebium.
# If not, see <https://www.gnu.org/licenses/>.

"""Client for the job-file-printer extension's typed API.

The job-file-printer extension writes what the BBC prints as one file per
print job in a directory on the host, a job ending after a quiet period in
emulated time. This client reads which job is being written and how many
have finished, and switches the printer online or offline -- the
scripting-friendly equivalent of the sidebar panel. Requires the server to be
launched with ``--job-file-printer directory=<dir>``.
"""

from __future__ import annotations

from dataclasses import dataclass

from beebium.client.extension import PeripheralExtensionAdapter
from beebium.ext.peripheral.job_file_printer._proto import job_file_printer_pb2

_SERVICE = "JobFilePrinter"


@dataclass(frozen=True)
class JobFilePrinterStatus:
    """The directory, the job being written, the counts and the online state."""

    directory: str  # where job files are written
    online: bool  # is the printer accepting bytes?
    current_job_file: str  # the job file being written, or "" between jobs
    bytes_in_job: int  # bytes written to the current job file
    jobs_completed: int  # jobs ended since launch
    total_bytes: int  # bytes written to all job files, after line-ending translation


class JobFilePrinter(PeripheralExtensionAdapter):
    """Read the job file printer's status and switch it online or offline.

    The JobFilePrinter messages are tunnelled over the core's ExtensionRpc channel.

    Usage:
        printer = bbc.extensions[JobFilePrinter]     # or JobFilePrinter.attach(bbc)
        printer.get_status().jobs_completed
    """

    EXTENSION_NAME = "job-file-printer"

    def get_status(self) -> JobFilePrinterStatus:
        """The directory, the job being written, the counts and the online state."""
        request = job_file_printer_pb2.JobFilePrinterGetStatusRequest()
        reply = self._invoke_bytes(_SERVICE, "GetStatus", request.SerializeToString())
        response = job_file_printer_pb2.JobFilePrinterStatus()
        response.ParseFromString(reply)
        return _status_from_proto(response)

    def set_online(self, online: bool) -> JobFilePrinterStatus:
        """Switch the printer online or offline.

        Offline, the receiver holds the byte the BBC strobed and withholds
        /ACK, so the guest waits as it would for a switched-off printer;
        online again, it accepts the byte and the print resumes. Returns the
        status after the change. Never blocks.
        """
        request = job_file_printer_pb2.JobFilePrinterSetOnlineRequest(online=online)
        reply = self._invoke_bytes(_SERVICE, "SetOnline", request.SerializeToString())
        response = job_file_printer_pb2.JobFilePrinterStatus()
        response.ParseFromString(reply)
        return _status_from_proto(response)


def _status_from_proto(
    response: job_file_printer_pb2.JobFilePrinterStatus,
) -> JobFilePrinterStatus:
    return JobFilePrinterStatus(
        directory=response.directory,
        online=response.online,
        current_job_file=response.current_job_file,
        bytes_in_job=response.bytes_in_job,
        jobs_completed=response.jobs_completed,
        total_bytes=response.total_bytes,
    )
