import asyncio
from . import _pythonodejs


async def event_loop_driver(node_instance):
    while True:
        is_alive = node_instance.poll()

        await asyncio.sleep(0)
