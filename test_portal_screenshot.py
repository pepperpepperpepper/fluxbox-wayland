#!/usr/bin/env python3
"""
Test script for Fluxbox Portal Screenshot functionality
"""
import dbus
import sys
import time

def test_portal_screenshot():
    print("🧪 Testing Fluxbox Portal Screenshot...")
    
    try:
        # Connect to session bus
        bus = dbus.SessionBus()
        
        # Get portal service
        portal = bus.get_object(
            'org.freedesktop.portal.Desktop',
            '/org/freedesktop/portal/desktop'
        )
        
        # Get screenshot interface
        screenshot_iface = dbus.Interface(
            portal,
            'org.freedesktop.impl.portal.Screenshot'
        )
        
        print("📡 Connected to portal service")
        
        # Take screenshot
        print("📸 Requesting screenshot...")
        
        response, results = screenshot_iface.Screenshot(
            "/test/handle",                    # handle
            "test-app",                        # app_id  
            "",                                # parent_window
            dbus.Dictionary({}, signature='sv') # options
        )
        
        if response == 0:  # Success
            uri = results.get('uri', '')
            print(f"✅ Screenshot successful!")
            print(f"📁 URI: {uri}")
            
            # Try to verify file exists
            if uri.startswith('file://'):
                file_path = uri[7:]  # Remove 'file://' prefix
                try:
                    import os
                    if os.path.exists(file_path):
                        size = os.path.getsize(file_path)
                        print(f"📏 File size: {size} bytes")
                        print(f"✅ Portal screenshot working perfectly!")
                        return True
                    else:
                        print(f"❌ File not found: {file_path}")
                except Exception as e:
                    print(f"❌ Error checking file: {e}")
        else:
            print(f"❌ Screenshot failed with response: {response}")
            
    except dbus.exceptions.DBusException as e:
        print(f"❌ D-Bus error: {e}")
        print("💡 Make sure fluxbox-portal-backend is running")
    except Exception as e:
        print(f"❌ Unexpected error: {e}")
    
    return False

def main():
    print("🌟 Fluxbox Portal Screenshot Test")
    print("=================================")
    
    if test_portal_screenshot():
        print("\n🎉 Portal test PASSED!")
        sys.exit(0)
    else:
        print("\n💥 Portal test FAILED!")
        sys.exit(1)

if __name__ == "__main__":
    main()