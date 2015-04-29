/* A CAN bus controller driver using virtio.
 *
 * Copyright 2015 Benoît Taine <benoit.taine@openwide.fr> Open Wide Ingénierie
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, see <http://www.gnu.org/licenses/>.
 */

#include <linux/netdevice.h>
#include <linux/can.h>
#include <linux/can/dev.h>
#include <linux/can/error.h>
#include <linux/clk.h>
#include <linux/kernel.h>
#include <linux/virtio.h>
#include <linux/virtio_can.h>

/* Communication over virtio-can uses little endian values, according to the
 * virtio spec.
 */

/* Structure of the message buffer */
struct virtcan_mb {
	u32 can_ctrl;
	u32 can_id;
	u64 data;
};

struct virtcan_priv {
	struct virtio_device *vdev;
	struct virtqueue     *cvq;
	struct can_priv      *can;
	struct napi_struct    napi;

	struct clk *clk_ipg;
	struct clk *clk_per;
};

static int virtcan_start_xmit(struct sk_buff *skb, struct net_device *dev)
{
	const struct virtcan_priv *priv = netdev_priv(dev);
	struct can_frame *cf = (struct can_frame *)skb->data;
	u32 can_id;

	if (can_dropped_invalid_skb(dev, skb))
		return NETDEV_TX_OK;

	netif_stop_queue(dev);

	can_put_echo_skb(skb, dev, 0);

	// TODO: Write to virtqueues

	return NETDEV_TX_OK;
}

static int virtcan_open(struct net_device *dev)
{
	struct virtcan_priv *priv = netdev_priv(dev);
	int err;

	err = open_candev(dev);
	if (err)
		return err;

	err = request_irq(dev->irq, virtcan_irq, IRQF_SHARED, dev->name, dev);
	if (err)
		goto out_close;

	err = virtcan_chip_start(dev);
	if (err)
		goto out_free_irq;

	napi_enable(&priv->napi);
	netif_start_queue(dev);

	return 0;

out_free_irq:
	free_irq(dev->irq, dev);
out_close:
	close_candev(dev);

	return err;
}

static int virtcan_close(struct net_device *dev)
{
	struct virtcan_priv *priv = netdev_priv(dev);

	netif_stop_queue(dev);
	napi_disable(&priv->napi);
	virtcan_chip_stop(dev);

	free_irq(dev->irq, dev);
	clk_disable_unprepare(priv->clk_per);
	clk_disable_unprepare(priv->clk_ipg);

	close_candev(dev);

	return 0;
}

static const struct net_device_ops virtcan_netdev_ops = {
	.ndo_open       = virtcan_open,
	.ndo_stop       = virtcan_stop,
	.ndo_start_xmit = virtcan_start_xmit,
	.ndo_change_mtu = can_change_mtu,
};

static int virtcan_probe(struct virtio_device *vdev)
{
	struct net_device   *dev;
	struct virtcan_priv *priv;
	u32 clock_freq = 0;

	dev = alloc_candev(sizeof(struct virtcan_priv), 1);
	if (!dev)
		return -ENOMEM;

	// TODO: Get clocks (timings, ...) through virtio config options

	priv = netdev_priv(dev);
	priv->can.clock.freq = clock_freq;

	virtio_device_ready(vdev);

	pr_debug("virtcan: registered device %s\n", dev->name);
}

static void virtnet_remove(struct virtio_device *vdev)
{
	struct virtcan_priv *vi = vdev->priv;
	struct net_device *dev = dev_get_drvdata(vdev->dev);

	unregister_netdev(dev);
	netif_napi_del(&vi->napi);
	free_netdev(dev);
}

static struct virtio_device_id id_table[] = {
	{ VIRTIO_ID_CAN, VIRTIO_DEV_ANY_ID },
	{ 0 },
};

static unsigned int features[] = {
	VIRTIO_CAN_F_CANFD,
};

static struct virtio_driver virtio_can_driver = {
	.feature_table      = features,
	.feature_table_size = ARRAY_SIZE(features),
	.driver.name        = KBUILD_MODNAME,
	.driver.owner       = THIS_MODULE,
	.id_table           = id_table,
	.probe              = virtcan_probe,
	.remove             = virtcan_remove,
	.config_changed     = virtcan_config_changed,
};

module_virtio_driver(virtio_can_driver);

MODULE_DEVICE_TABLE(virtio, id_table);
MODULE_DESCRIPTION("Virtio CAN bus driver");
MODULE_LICENSE("GPL");

