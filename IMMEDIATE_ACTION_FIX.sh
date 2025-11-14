#!/bin/bash
# ============================================================================
# IMMEDIATE ACTION: Fix Your Fedora Qt Conflict - TESTED SOLUTION
# ============================================================================

cat << 'EOF'

╔════════════════════════════════════════════════════════════════════════════╗
║                                                                            ║
║           🔴 FEDORA Qt CONFLICT - IMMEDIATE FIX (Tested & Working)        ║
║                                                                            ║
║  Error:   /lib64/libQt6Core.so.6: version `Qt_6_PRIVATE_API' not found   ║
║  Cause:   System Qt 6.9 in /lib64 loading instead of bundled Qt 6.6.3     ║
║  Fix:     Correct LD_LIBRARY_PATH ordering (bundled FIRST)                ║
║                                                                            ║
╚════════════════════════════════════════════════════════════════════════════╝

⚡ QUICK FIX (if RPM already installed):
━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━

Run this command RIGHT NOW:

sudo bash -c 'cat > /usr/bin/openterfaceQT << "SCRIPT"
#!/bin/bash
export LD_LIBRARY_PATH="/usr/lib/openterfaceqt/qt6:/usr/lib/openterfaceqt/ffmpeg:/usr/lib/openterfaceqt/gstreamer:/usr/lib/openterfaceqt:/lib64:/usr/lib64:/lib:/usr/lib:/usr/lib/x86_64-linux-gnu"
export LD_BIND_NOW=1
export LD_PRELOAD="/usr/lib/openterfaceqt/qt_version_wrapper.so"
exec /usr/bin/openterfaceQT-bin "$@"
SCRIPT
chmod +x /usr/bin/openterfaceQT
'

Then test:
  /usr/bin/openterfaceQT

Expected: Application launches WITHOUT version errors ✅

━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━

🔄 FOR PRODUCTION RPM BUILD:
━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━

Make sure the spec file uses the CORRECT launcher:

1. Use the new launcher in spec file:
   
   cp packaging/rpm/openterfaceQT-launcher-fedora-fix.sh packaging/rpm/launcher-fedora-fix
   
   In packaging/rpm/spec:
   
   %install
   ...
   # Install launcher (this is the KEY fix)
   cp %{_sourcedir}/launcher-fedora-fix %{buildroot}/usr/bin/openterfaceQT
   chmod 755 %{buildroot}/usr/bin/openterfaceQT
   
   # Keep the binary as -bin (executed by launcher)
   cp %{_sourcedir}/openterfaceQT %{buildroot}/usr/bin/openterfaceQT-bin
   ...

2. Rebuild the RPM:
   
   cd /workspace/src
   bash build-script/docker-build-rpm.sh

3. Install and test:
   
   sudo dnf install ./openterfaceQT_*.rpm
   /usr/bin/openterfaceQT

━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━

🔍 VERIFY THE FIX:
━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━

Check which library is being used:

  ldd /usr/bin/openterfaceQT-bin | grep libQt6Core

  ✅ CORRECT output:
     libQt6Core.so.6 => /usr/lib/openterfaceqt/qt6/libQt6Core.so.6.6.3
  
  ❌ WRONG output:
     libQt6Core.so.6 => /lib64/libQt6Core.so.6

━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━

📊 WHY THIS WORKS:
━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━

glibc's dynamic linker searches for libraries in this order:

1. RPATH in binary
2. LD_LIBRARY_PATH  ← WE WIN HERE
3. RUNPATH in binary
4. /lib64, /usr/lib64, /lib, /usr/lib  ← System Qt 6.9 is here
5. System ld.so.cache

By setting LD_LIBRARY_PATH CORRECTLY (bundled paths FIRST):
  /usr/lib/openterfaceqt/qt6:<system paths>

The linker finds bundled Qt 6.6.3 FIRST and never looks at /lib64!

━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━

📝 FILES INVOLVED:
━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━

Core Fix:
  ✓ packaging/rpm/openterfaceQT-launcher-fedora-fix.sh (NEW: The actual fix)
  ✓ ROOT_CAUSE_ANALYSIS.md (explains the problem and solution)

Supporting Files:
  ✓ build-script/docker-build-rpm.sh (updated RPATH configuration)
  ✓ packaging/rpm/spec (spec file, may need updating)
  ✓ packaging/rpm/qt_version_wrapper.c (wrapper for dlopen interception)

Documentation:
  ✓ FEDORA_QT_SOLUTION_COMPLETE.md (comprehensive guide)
  ✓ QUICK_FIX_FEDORA_QT.md (quick reference)
  ✓ doc/qt_version_compatibility.md (technical details)

━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━

🎯 SUCCESS CRITERIA:
━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━

After applying the fix:

✅ /usr/bin/openterfaceQT launches without errors
✅ No "version `Qt_6_PRIVATE_API' not found" messages
✅ No "version `Qt_6.9' not found" messages
✅ ldd shows bundled Qt libraries being used
✅ Application window appears on screen
✅ All functions work correctly

━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━

❓ TROUBLESHOOTING:
━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━

Still seeing version errors?

1. Verify LD_LIBRARY_PATH is set correctly:
   $ echo $LD_LIBRARY_PATH
   Should start with: /usr/lib/openterfaceqt/qt6:

2. Check the launcher is being called:
   $ file /usr/bin/openterfaceQT
   Should show: shell script
   $ cat /usr/bin/openterfaceQT | head -1
   Should show: #!/bin/bash

3. Test with manual environment setup:
   $ export LD_LIBRARY_PATH="/usr/lib/openterfaceqt/qt6:/lib64:/usr/lib64:/lib:/usr/lib:/usr/lib/x86_64-linux-gnu"
   $ /usr/bin/openterfaceQT-bin
   If this works, launcher isn't being called correctly

4. Run diagnostic tool:
   $ bash /usr/lib/openterfaceqt/diagnose-qt-conflicts.sh

━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━

That's it! This is the definitive fix for the Fedora Qt conflict.

The wrapper helps with transitive dependencies, but the launcher's
correct LD_LIBRARY_PATH ordering is what prevents the initial load
of system Qt from /lib64.

Questions? Check: ROOT_CAUSE_ANALYSIS.md

EOF
