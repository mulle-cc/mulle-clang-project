#!/bin/bash

set -e

# Check if a suitable debian VM is already running
for name in bookworm trixie; do
   state="$(virsh domstate "${name}" 2>/dev/null || true)"
   if [ "${state}" = "running" ]; then
      echo "VM '${name}' is already running"
      if ssh -o ConnectTimeout=10 -o BatchMode=yes "${name}" "uname -a" 2>/dev/null; then
         echo "VM '${name}' is reachable via SSH - good to go"
         exit 0
      fi
   fi
done

# Try to start a stopped VM
for name in bookworm trixie; do
   state="$(virsh domstate "${name}" 2>/dev/null || true)"
   if [ "${state}" = "shut off" ]; then
      echo "Starting VM '${name}'..."
      virsh start "${name}"
      echo "Waiting for SSH..."
      for i in $(seq 1 30); do
         if ssh -o ConnectTimeout=5 -o BatchMode=yes "${name}" "uname -a" 2>/dev/null; then
            echo "VM '${name}' is up and reachable"
            exit 0
         fi
         sleep 5
      done
      echo "VM '${name}' started but SSH not reachable after 150s" >&2
      exit 1
   fi
done

echo "No known debian VM (bookworm/trixie) found in virsh. Please create one manually." >&2
virsh list --all
exit 1
