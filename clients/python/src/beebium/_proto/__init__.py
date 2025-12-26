# Copyright 2025 Robert Smallshire <robert@smallshire.org.uk>
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

"""Generated protocol buffer modules.

Run scripts/generate_proto.sh to regenerate these files from the .proto definitions.
"""

# These imports will be available after running generate_proto.sh
try:
    from beebium._proto import video_pb2, video_pb2_grpc
    from beebium._proto import keyboard_pb2, keyboard_pb2_grpc
    from beebium._proto import debugger_pb2, debugger_pb2_grpc
except ImportError:
    # Proto files not yet generated
    pass
