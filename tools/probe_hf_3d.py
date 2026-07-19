from gradio_client import Client
import traceback
try:
    client = Client("stabilityai/TripoSR")
    print("API:", client.view_api())
except Exception as e:
    traceback.print_exc()
    # try alternate spaces
    for space in ["tencent/Hunyuan3D-2", "VAST-AI/TripoSG", "ashawkey/LGM"]:
        try:
            print("Trying", space)
            c = Client(space)
            print(space, "OK")
            print(c.view_api(return_format="dict") if hasattr(c,'view_api') else c.view_api())
            break
        except Exception as e2:
            print(space, "fail", e2)
