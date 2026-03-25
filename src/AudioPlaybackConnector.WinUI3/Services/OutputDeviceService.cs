using AudioPlaybackConnector.WinUI3.Interop;
using AudioPlaybackConnector.WinUI3.Models;
using System.Runtime.InteropServices;

namespace AudioPlaybackConnector.WinUI3.Services;

public sealed class OutputDeviceService
{
    public Task<IReadOnlyList<OutputDeviceInfo>> GetRenderDevicesAsync()
    {
        return Task.Run<IReadOnlyList<OutputDeviceInfo>>(EnumerateRenderDevices);
    }

    private static IReadOnlyList<OutputDeviceInfo> EnumerateRenderDevices()
    {
        IMMDeviceEnumerator? enumerator = null;
        IMMDevice? defaultDevice = null;
        IMMDeviceCollection? collection = null;

        try
        {
            enumerator = (IMMDeviceEnumerator)(object)new MMDeviceEnumeratorComObject();

            Marshal.ThrowExceptionForHR(enumerator.GetDefaultAudioEndpoint(EDataFlow.eRender, ERole.eMultimedia, out defaultDevice));
            var defaultDeviceId = GetDeviceId(defaultDevice);

            Marshal.ThrowExceptionForHR(enumerator.EnumAudioEndpoints(EDataFlow.eRender, DeviceState.Active, out collection));
            Marshal.ThrowExceptionForHR(collection.GetCount(out var count));

            var devices = new List<OutputDeviceInfo>((int)count);
            for (uint index = 0; index < count; index++)
            {
                IMMDevice? device = null;
                try
                {
                    Marshal.ThrowExceptionForHR(collection.Item(index, out device));
                    var deviceId = GetDeviceId(device);
                    devices.Add(new OutputDeviceInfo
                    {
                        Id = deviceId,
                        Name = GetDeviceFriendlyName(device),
                        IsDefault = string.Equals(deviceId, defaultDeviceId, StringComparison.Ordinal)
                    });
                }
                finally
                {
                    ReleaseCom(device);
                }
            }

            return devices
                .OrderByDescending(device => device.IsDefault)
                .ThenBy(device => device.DisplayName, StringComparer.CurrentCultureIgnoreCase)
                .ToList();
        }
        finally
        {
            ReleaseCom(collection);
            ReleaseCom(defaultDevice);
            ReleaseCom(enumerator);
        }
    }

    private static string GetDeviceId(IMMDevice device)
    {
        Marshal.ThrowExceptionForHR(device.GetId(out var idPointer));
        try
        {
            return Marshal.PtrToStringUni(idPointer) ?? string.Empty;
        }
        finally
        {
            Marshal.FreeCoTaskMem(idPointer);
        }
    }

    private static string GetDeviceFriendlyName(IMMDevice device)
    {
        IPropertyStore? propertyStore = null;
        try
        {
            Marshal.ThrowExceptionForHR(device.OpenPropertyStore(MMDeviceConstants.STGM_READ, out propertyStore));
            var key = MMDeviceConstants.PKEY_Device_FriendlyName;
            Marshal.ThrowExceptionForHR(propertyStore.GetValue(ref key, out var value));

            try
            {
                return value.GetString() ?? "Unknown output device";
            }
            finally
            {
                MMDeviceConstants.PropVariantClear(ref value);
            }
        }
        finally
        {
            ReleaseCom(propertyStore);
        }
    }

    private static void ReleaseCom(object? comObject)
    {
        if (comObject is not null && Marshal.IsComObject(comObject))
        {
            Marshal.FinalReleaseComObject(comObject);
        }
    }
}
